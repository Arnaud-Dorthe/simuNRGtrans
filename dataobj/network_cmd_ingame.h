#ifndef _NETWORK_CMD_INGAME_H_
#define _NETWORK_CMD_INGAME_H_

#include "network_cmd.h"
#include "../simworld.h"
#include "../tpl/slist_tpl.h"
#include "koord3d.h"
#include "../path_explorer.h"

class memory_rw_t;
class packet_t;
class spieler_t;
class werkzeug_t;

/**
 * nwc_gameinfo_t
 * @from-client: client wants map info
 *		server sends nwc_gameinfo_t to sender
 * @from-server:
 *		@data len of gameinfo
 *		client processes this in network_connect
 */
class nwc_gameinfo_t : public network_command_t {
public:
	nwc_gameinfo_t() : network_command_t(NWC_GAMEINFO) { len = 0; }
	virtual bool execute(karte_t *);
	virtual void rdwr();
	virtual const char* get_name() { return "nwc_gameinfo_t";}
	uint32 client_id;
	uint32 len;
};

/**
 * nwc_join_t
 * @from-client: client wants to join the server
 *		server sends nwc_join_t to sender, nwc_sync_t to all clients
 * @from-server:
 *		@data answer == 1 (if joining now is ok)
 *		@data client_id
 *		client ignores the following nwc_sync_t, waits for nwc_ready_t
 */
class nwc_join_t : public network_command_t {
public:
	nwc_join_t() : network_command_t(NWC_JOIN), client_id(0), answer(0) {}
	virtual bool execute(karte_t *);
	virtual void rdwr();
	virtual const char* get_name() { return "nwc_join_t";}
	uint32 client_id;
	uint8 answer;

	/**
	 * this clients is in the process of joining
	 */
	static SOCKET pending_join_client;

	static bool is_pending() { return pending_join_client != INVALID_SOCKET; }
};

/**
 * nwc_ready_t
 * @from-client:
 *		@data sync_steps at which client will continue
 *		client paused, waits for unpause
 * @from-server:
 *		data is resent to client
 *		map_counter to identify network_commands
 *		unpause client
 */
class nwc_ready_t : public network_command_t {
public:
	nwc_ready_t() : network_command_t(NWC_READY), sync_step(0), map_counter(0) { }
	nwc_ready_t(uint32 sync_step_, uint32 map_counter_, const checklist_t &checklist_) : network_command_t(NWC_READY), sync_step(sync_step_), map_counter(map_counter_), checklist(checklist_) { }
	virtual bool execute(karte_t *);
	virtual void rdwr();
	virtual const char* get_name() { return "nwc_ready_t";}
	uint32 sync_step;
	uint32 map_counter;
	checklist_t checklist;

	static void append_map_counter(uint32 map_counter_);
	static void clear_map_counters();
private:
	static vector_tpl<uint32>all_map_counters;
};

/**
 * nwc_game_t
 * @from-server:
 *		@data len of savegame
 *		client processes this in network_connect
 */
class nwc_game_t : public network_command_t {
public:
	nwc_game_t(uint32 len_=0) : network_command_t(NWC_GAME), len(len_) {}
	virtual void rdwr();
	virtual const char* get_name() { return "nwc_game_t";}
	uint32 len;
};

/**
 * commands that have to be executed at a certain sync_step
 */
class network_world_command_t : public network_command_t {
public:
	network_world_command_t() : network_command_t(), sync_step(0), map_counter(0) {};
	network_world_command_t(uint16 /*id*/, uint32 /*sync_step*/, uint32 /*map_counter*/);
	virtual void rdwr();
	virtual const char* get_name() { return "network_world_command_t";}
	// put it to the command queue
	virtual bool execute(karte_t *);
	// apply it to the world
	virtual void do_command(karte_t*) {}
	uint32 get_sync_step() const { return sync_step; }
	uint32 get_map_counter() const { return map_counter; }
	// ignore events that lie in the past?
	// if false: any cmd with sync_step < world->sync_step forces network disconnect
	virtual bool ignore_old_events() const { return false;}
	// for sorted data structures
	bool operator <= (network_world_command_t c) const { return sync_step <= c.sync_step; }
	static bool cmp(network_world_command_t *nwc1, network_world_command_t *nwc2) { return nwc1->get_sync_step() <= nwc2->get_sync_step(); }
protected:
	uint32 sync_step; // when this has to be executed
	uint32 map_counter; // cmd comes from world at this stage
	// TODO: uint16 sub_step to have an order within one step
};

/**
 * nwc_sync_t
 * @from-server:
 *		@data client_id this client wants to receive the game
 *		@data new_map_counter new map counter for the new world after game reloading
 *		clients: pause game, save, load, wait for nwc_ready_t command to unpause
 *		server: pause game, save, load, send game to client, send nwc_ready_t command to client
 */
class nwc_sync_t : public network_world_command_t {
public:
	nwc_sync_t() : network_world_command_t(NWC_SYNC, 0, 0), client_id(0), new_map_counter(0) {};
	nwc_sync_t(uint32 sync_steps, uint32 map_counter, uint32 send_to_client, uint32 _new_map_counter) : network_world_command_t(NWC_SYNC, sync_steps, map_counter), client_id(send_to_client), new_map_counter(_new_map_counter) { }
	virtual void rdwr();
	virtual void do_command(karte_t*);
	virtual const char* get_name() { return "nwc_sync_t"; }
	uint32 get_new_map_counter() const { return new_map_counter; }
private:
	uint32 client_id; // this client shall receive the game
	uint32 new_map_counter;	// map counter to be applied to the new world after game reloading
};

/**
 * nwc_routesearch_t
 * @from-client:
 *		@data updated set of limits from the client
 * @from-server:
 *		@data min limit set to be applied to the client
 * @author Knightly
 */
class nwc_routesearch_t : public network_world_command_t {
public:
	nwc_routesearch_t() : network_world_command_t(NWC_ROUTESEARCH, 0, 0), apply_limits(false) { };
	nwc_routesearch_t(uint32 sync_step, uint32 map_counter, path_explorer_t::limit_set_t &_limit_set, bool _apply_limits)
		: network_world_command_t(NWC_ROUTESEARCH, sync_step, map_counter), limit_set(_limit_set), apply_limits(_apply_limits) { }
	virtual const char* get_name() { return "nwc_routesearch_t"; }
	virtual void rdwr();
	virtual bool execute(karte_t *world);
	virtual void do_command(karte_t *world);

	static void check_for_transmission(karte_t *world);
	static bool transmit_active_limit_set(SOCKET client_socket, uint32 sync_step, uint32 map_counter);
	static void remove_client_entry(uint32 client_id);
	static void reset();
private:
	path_explorer_t::limit_set_t limit_set;
	bool apply_limits;

	struct client_entry_t {
		uint32 client_id;
		path_explorer_t::limit_set_t limit_set;
		
		client_entry_t() : client_id(0xFFFFFFFF) { }
		client_entry_t(uint32 _client_id, path_explorer_t::limit_set_t &_limit_set) : client_id(_client_id), limit_set(_limit_set) { }
	};

	static slist_tpl<client_entry_t> client_entries;
	static path_explorer_t::limit_set_t active_limit_set;
	static path_explorer_t::limit_set_t min_limit_set;
	static uint32 last_update_sync_step;
	static uint16 accumulated_updates;

	static path_explorer_t::limit_set_t calc_min_limits();
};

/**
 * nwc_check_t
 * @from-server:
 *		@data checklist random seed and quickstone next check entries at previous sync_step
 *		clients: check random seed, if check fails disconnect.
 *      the check is done in karte_t::interactive
 */
class nwc_check_t : public network_world_command_t {
public:
	nwc_check_t() : network_world_command_t(NWC_CHECK, 0, 0), server_sync_step(0) { }
	nwc_check_t(uint32 sync_steps, uint32 map_counter, const checklist_t &server_checklist_, uint32 server_sync_step_) : network_world_command_t(NWC_CHECK, sync_steps, map_counter), server_checklist(server_checklist_), server_sync_step(server_sync_step_) {};
	virtual void rdwr();
	virtual void do_command(karte_t*) { }
	virtual const char* get_name() { return "nwc_check_t"; }
	checklist_t server_checklist;
	uint32 server_sync_step;
	// no action required -> can be ignored if too old
	virtual bool ignore_old_events() const { return true; }
};

/**
 * nwc_tool_t
 * @from-client: client sends tool init/work
 * @from-server: server sends nwc_tool_t to all clients with step when tool has to be executed
 *		@data client_id (the client that launched the tool, sent by server)
 *		@data player_nr
 *		@data init (if true call init else work)
 *		@data wkz_id
 *		@data pos
 *		@data default_param
 *		@data exec (if true executes, else server sends it to clients)
 */

class nwc_tool_t : public network_world_command_t {
public:
	// to detect desync we sent these infos always together (only valid for tools)
	checklist_t last_checklist;
	uint32 last_sync_step;

	nwc_tool_t();
	nwc_tool_t(spieler_t *sp, werkzeug_t *wkz, koord3d pos, uint32 sync_steps, uint32 map_counter, bool init);
	nwc_tool_t(const nwc_tool_t&);

	// messages are allowed to arrive at any time
	virtual bool ignore_old_events() const;

	virtual ~nwc_tool_t();

	virtual void rdwr();
	// send to clients or put to command queue (depending on exec)
	virtual bool execute(karte_t *);
	// really executes it, here exec should be true
	virtual void do_command(karte_t*);
	virtual const char* get_name() { return "nwc_tool_t"; }
	bool is_from_initiator() const { return !exec; }
private:
	char *default_param;
	uint32 tool_client_id;
	uint16 wkz_id;
	koord3d pos;
	uint8 flags;
	uint8 player_nr;
	bool init;
	bool exec;

	uint8 custom_data_buf[256];
	memory_rw_t *custom_data;

	// compare default_param's (NULL pointers allowed)
	// @return true if default_param are equal
	static bool cmp_default_param(const char *d1, const char *d2);

	/**
	 * contains tools of players at other clients:
	 *   for every player at every client we store the active tool in this node class
	 *
	 * the member variable default_param saves the default-parameter of the tool,
	 * i.e. wkz->default_param == default_param
	 *
	 * default_param has its own simple memory management
	 */
	class tool_node_t {
	private:
		const char* default_param;
		werkzeug_t *wkz;
		// own memory management for default_param
		void set_default_param(const char* param);
		void set_tool(werkzeug_t *wkz_);
	public:
		uint32 client_id;
		uint8 player_id;
		tool_node_t() : default_param(NULL), wkz(NULL), client_id(0), player_id(255) {}
		tool_node_t(werkzeug_t *_wkz, uint8 _player_id, uint32 _client_id) : default_param(NULL), wkz(_wkz), client_id(_client_id), player_id(_player_id) {}

		const char* get_default_param() const { return default_param;}

		werkzeug_t* get_tool() const { return wkz;}

		/**
		 * mimics void karte_t::local_set_werkzeug(werkzeug_t *, spieler_t *)
		 * deletes wkz_new if wkz_new->init() returns false and store is false
		 */
		void client_set_werkzeug(werkzeug_t * &wkz_new, const char* default_param_, bool store, karte_t*, spieler_t*);

		/**
		 * @return true if ids (player_id and client_id) of both tool_node_t's are equal
		 */
		inline bool operator == (const tool_node_t c) const { return client_id==c.client_id  &&  player_id==c.player_id; }
	};

	// static list of active tools for each pair (client_id, player_id)
	static vector_tpl<tool_node_t> tool_list;
};

#endif