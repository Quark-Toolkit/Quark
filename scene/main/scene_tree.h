/*************************************************************************/
/*  scene_tree.h                                                         */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2018 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2018 Godot Engine contributors (cf. AUTHORS.md)    */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#ifndef SCENE_MAIN_LOOP_H
#define SCENE_MAIN_LOOP_H

#include "os/main_loop.h"
#include "os/thread_safe.h"
#include "scene/resources/world_2d.h"
#include "self_list.h"

/**
	@author Juan Linietsky <reduzio@gmail.com>
*/

class SceneTree;
class Node;
class Viewport;
class Material;

class SceneTreeTimer : public Reference {
	GDCLASS(SceneTreeTimer, Reference);

	float time_left;
	bool process_pause;

protected:
	static void _bind_methods();

public:
	void set_time_left(float p_time);
	float get_time_left() const;

	void set_pause_mode_process(bool p_pause_mode_process);
	bool is_pause_mode_process();

	SceneTreeTimer();
};

class SceneTree : public MainLoop {

	_THREAD_SAFE_CLASS_

	GDCLASS(SceneTree, MainLoop);

public:
	typedef void (*IdleCallback)();

	enum StretchMode {

		STRETCH_MODE_DISABLED,
		STRETCH_MODE_2D,
		STRETCH_MODE_VIEWPORT,
	};

	enum StretchAspect {

		STRETCH_ASPECT_IGNORE,
		STRETCH_ASPECT_KEEP,
		STRETCH_ASPECT_KEEP_WIDTH,
		STRETCH_ASPECT_KEEP_HEIGHT,
		STRETCH_ASPECT_EXPAND,
	};

private:
	struct Group {

		Vector<Node *> nodes;
		//uint64_t last_tree_version;
		bool changed;
		Group() { changed = false; };
	};

	Viewport *root;

	uint64_t tree_version;
	float fixed_process_time;
	float idle_process_time;
	bool accept_quit;
	bool quit_on_go_back;

#ifdef DEBUG_ENABLED
	bool debug_collisions_hint;
	bool debug_navigation_hint;
#endif
	bool pause;
	int root_lock;

	Map<StringName, Group> group_map;
	bool _quit;
	bool initialized;
	bool input_handled;

	Size2 last_screen_size;
	StringName tree_changed_name;
	StringName node_added_name;
	StringName node_removed_name;

	bool use_font_oversampling;
	int64_t current_frame;
	int64_t current_event;
	int node_count;

#ifdef TOOLS_ENABLED
	Node *edited_scene_root;
#endif
	struct UGCall {

		StringName group;
		StringName call;

		bool operator<(const UGCall &p_with) const { return group == p_with.group ? call < p_with.call : group < p_with.group; }
	};

	//safety for when a node is deleted while a group is being called
	int call_lock;
	Set<Node *> call_skip; //skip erased nodes

	StretchMode stretch_mode;
	StretchAspect stretch_aspect;
	Size2i stretch_min;
	real_t stretch_shrink;

	void _update_root_rect();

	List<ObjectID> delete_queue;

	Map<UGCall, Vector<Variant> > unique_group_calls;
	bool ugc_locked;
	void _flush_ugc();

	_FORCE_INLINE_ void _update_group_order(Group &g);
	void _update_listener();

	Array _get_nodes_in_group(const StringName &p_group);

	Node *current_scene;

	Color debug_collisions_color;
	Color debug_collision_contact_color;
	Color debug_navigation_color;
	Color debug_navigation_disabled_color;
	Ref<Material> navigation_material;
	Ref<Material> navigation_disabled_material;
	Ref<Material> collision_material;
	int collision_debug_contacts;

	//void _call_group(uint32_t p_call_flags,const StringName& p_group,const StringName& p_function,const Variant& p_arg1,const Variant& p_arg2);

	List<Ref<SceneTreeTimer> > timers;

	//path get caches
	struct PathGetCache {
		struct NodeInfo {
			NodePath path;
			ObjectID instance;
		};

		Map<int, NodeInfo> nodes;
	};

	Map<int, PathGetCache> path_get_cache;

	Vector<uint8_t> packet_cache;

	static SceneTree *singleton;
	friend class Node;

	void tree_changed();
	void node_added(Node *p_node);
	void node_removed(Node *p_node);

	Group *add_to_group(const StringName &p_group, Node *p_node);
	void remove_from_group(const StringName &p_group, Node *p_node);

	void _notify_group_pause(const StringName &p_group, int p_notification);
	void _call_input_pause(const StringName &p_group, const StringName &p_method, const Ref<InputEvent> &p_input);
	Variant _call_group_flags(const Variant **p_args, int p_argcount, Variant::CallError &r_error);
	Variant _call_group(const Variant **p_args, int p_argcount, Variant::CallError &r_error);

	void _flush_delete_queue();
	//optimization
	friend class CanvasItem;
	friend class Spatial;
	friend class Viewport;

	SelfList<Node>::List xform_change_list;

	enum {
		MAX_IDLE_CALLBACKS = 256
	};

	static IdleCallback idle_callbacks[MAX_IDLE_CALLBACKS];
	static int idle_callback_count;
	void _call_idle_callbacks();

protected:
	void _notification(int p_notification);
	static void _bind_methods();

public:
	enum {
		NOTIFICATION_TRANSFORM_CHANGED = 29
	};

	enum GroupCallFlags {
		GROUP_CALL_DEFAULT = 0,
		GROUP_CALL_REVERSE = 1,
		GROUP_CALL_REALTIME = 2,
		GROUP_CALL_UNIQUE = 4,
		GROUP_CALL_MULTILEVEL = 8,
	};

	_FORCE_INLINE_ Viewport *get_root() const { return root; }

	void call_group_flags(uint32_t p_call_flags, const StringName &p_group, const StringName &p_function, VARIANT_ARG_LIST);
	void notify_group_flags(uint32_t p_call_flags, const StringName &p_group, int p_notification);
	void set_group_flags(uint32_t p_call_flags, const StringName &p_group, const String &p_name, const Variant &p_value);

	void call_group(const StringName &p_group, const StringName &p_function, VARIANT_ARG_LIST);
	void notify_group(const StringName &p_group, int p_notification);
	void set_group(const StringName &p_group, const String &p_name, const Variant &p_value);

	void flush_transform_notifications();

	virtual void input_text(const String &p_text);
	virtual void input_event(const Ref<InputEvent> &p_event);
	virtual void init();

	virtual bool iteration(float p_time);
	virtual bool idle(float p_time);

	virtual void finish();

	void set_auto_accept_quit(bool p_enable);
	void set_quit_on_go_back(bool p_enable);

	void quit();

	void set_input_as_handled();
	bool is_input_handled();
	_FORCE_INLINE_ float get_fixed_process_time() const { return fixed_process_time; }
	_FORCE_INLINE_ float get_idle_process_time() const { return idle_process_time; }

#ifdef TOOLS_ENABLED
	bool is_node_being_edited(const Node *p_node) const;
#else
	bool is_node_being_edited(const Node *p_node) const { return false; }
#endif

	void set_pause(bool p_enabled);
	bool is_paused() const;

	void set_camera(const RID &p_camera);
	RID get_camera() const;

#ifdef DEBUG_ENABLED
	void set_debug_collisions_hint(bool p_enabled);
	bool is_debugging_collisions_hint() const;

	void set_debug_navigation_hint(bool p_enabled);
	bool is_debugging_navigation_hint() const;
#else
	void set_debug_collisions_hint(bool p_enabled) {}
	bool is_debugging_collisions_hint() const { return false; }

	void set_debug_navigation_hint(bool p_enabled) {}
	bool is_debugging_navigation_hint() const { return false; }
#endif

	void set_debug_collisions_color(const Color &p_color);
	Color get_debug_collisions_color() const;

	void set_debug_collision_contact_color(const Color &p_color);
	Color get_debug_collision_contact_color() const;

	void set_debug_navigation_color(const Color &p_color);
	Color get_debug_navigation_color() const;

	void set_debug_navigation_disabled_color(const Color &p_color);
	Color get_debug_navigation_disabled_color() const;

	Ref<Material> get_debug_navigation_material();
	Ref<Material> get_debug_navigation_disabled_material();
	Ref<Material> get_debug_collision_material();

	int get_collision_debug_contact_count() { return collision_debug_contacts; }

	int64_t get_frame() const;
	int64_t get_event_count() const;

	int get_node_count() const;

	void queue_delete(Object *p_object);

	void get_nodes_in_group(const StringName &p_group, List<Node *> *p_list);
	bool has_group(const StringName &p_identifier) const;

	void set_screen_stretch(StretchMode p_mode, StretchAspect p_aspect, const Size2 p_minsize, real_t p_shrink = 1);

	void set_use_font_oversampling(bool p_oversampling);
	bool is_using_font_oversampling() const;

#ifdef TOOLS_ENABLED
	void set_edited_scene_root(Node *p_node);
	Node *get_edited_scene_root() const;
#endif

	void set_current_scene(Node *p_scene);
	Node *get_current_scene() const;
	Error reload_current_scene();
	void change_scene(Node *p_to);

	Ref<SceneTreeTimer> create_timer(float p_delay_sec, bool p_process_pause = true);

	//used by Main::start, don't use otherwise
	void add_current_scene(Node *p_current);

	static SceneTree *get_singleton() { return singleton; }

	void drop_files(const Vector<String> &p_files, int p_from_screen = 0);

	static void add_idle_callback(IdleCallback p_callback);
	SceneTree();
	~SceneTree();
};

VARIANT_ENUM_CAST(SceneTree::StretchMode);
VARIANT_ENUM_CAST(SceneTree::StretchAspect);
VARIANT_ENUM_CAST(SceneTree::GroupCallFlags);

#endif
