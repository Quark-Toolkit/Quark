/*************************************************************************/
/*  node.cpp                                                             */
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

#include "node.h"

#include "core/core_string_names.h"
#include "io/resource_loader.h"
#include "message_queue.h"
#include "print_string.h"
#include "scene/scene_string_names.h"
#include "viewport.h"

VARIANT_ENUM_CAST(Node::PauseMode);

void Node::_notification(int p_notification) {

	switch (p_notification) {

		case NOTIFICATION_PROCESS: {

			if (get_script_instance()) {

				Variant time = get_process_delta_time();
				const Variant *ptr[1] = { &time };
				get_script_instance()->call_multilevel(SceneStringNames::get_singleton()->_process, ptr, 1);
			}
		} break;
		case NOTIFICATION_FIXED_PROCESS: {

			if (get_script_instance()) {

				Variant time = get_fixed_process_delta_time();
				const Variant *ptr[1] = { &time };
				get_script_instance()->call_multilevel(SceneStringNames::get_singleton()->_fixed_process, ptr, 1);
			}

		} break;
		case NOTIFICATION_ENTER_TREE: {

			if (data.pause_mode == PAUSE_MODE_INHERIT) {

				if (data.parent)
					data.pause_owner = data.parent->data.pause_owner;
				else
					data.pause_owner = NULL;
			} else {
				data.pause_owner = this;
			}

			if (data.input)
				add_to_group("_vp_input" + itos(get_viewport()->get_instance_id()));
			if (data.unhandled_input)
				add_to_group("_vp_unhandled_input" + itos(get_viewport()->get_instance_id()));
			if (data.unhandled_key_input)
				add_to_group("_vp_unhandled_key_input" + itos(get_viewport()->get_instance_id()));

			get_tree()->node_count++;

		} break;
		case NOTIFICATION_EXIT_TREE: {

			get_tree()->node_count--;
			if (data.input)
				remove_from_group("_vp_input" + itos(get_viewport()->get_instance_id()));
			if (data.unhandled_input)
				remove_from_group("_vp_unhandled_input" + itos(get_viewport()->get_instance_id()));
			if (data.unhandled_key_input)
				remove_from_group("_vp_unhandled_key_input" + itos(get_viewport()->get_instance_id()));

			data.pause_owner = NULL;
			if (data.path_cache) {
				memdelete(data.path_cache);
				data.path_cache = NULL;
			}
		} break;
		case NOTIFICATION_PATH_CHANGED: {

			if (data.path_cache) {
				memdelete(data.path_cache);
				data.path_cache = NULL;
			}
		} break;
		case NOTIFICATION_READY: {

			if (get_script_instance()) {

				if (get_script_instance()->has_method(SceneStringNames::get_singleton()->_input)) {
					set_process_input(true);
				}

				if (get_script_instance()->has_method(SceneStringNames::get_singleton()->_unhandled_input)) {
					set_process_unhandled_input(true);
				}

				if (get_script_instance()->has_method(SceneStringNames::get_singleton()->_unhandled_key_input)) {
					set_process_unhandled_key_input(true);
				}

				if (get_script_instance()->has_method(SceneStringNames::get_singleton()->_process)) {
					set_process(true);
				}

				if (get_script_instance()->has_method(SceneStringNames::get_singleton()->_fixed_process)) {
					set_fixed_process(true);
				}

				get_script_instance()->call_multilevel_reversed(SceneStringNames::get_singleton()->_ready, NULL, 0);
			}
			//emit_signal(SceneStringNames::get_singleton()->enter_tree);

		} break;
		case NOTIFICATION_POSTINITIALIZE: {
			data.in_constructor = false;
		} break;
		case NOTIFICATION_PREDELETE: {

			set_owner(NULL);

			while (data.owned.size()) {

				data.owned.front()->get()->set_owner(NULL);
			}

			if (data.parent) {

				data.parent->remove_child(this);
			}

			// kill children as cleanly as possible
			while (data.children.size()) {

				Node *child = data.children[0];
				remove_child(child);
				memdelete(child);
			}

		} break;
	}
}

void Node::_propagate_ready() {

	data.ready_notified = true;
	data.blocked++;
	for (int i = 0; i < data.children.size(); i++) {

		data.children[i]->_propagate_ready();
	}
	data.blocked--;
	if (data.ready_first) {
		data.ready_first = false;
		notification(NOTIFICATION_READY);
	}
}

void Node::_propagate_enter_tree() {
	// this needs to happen to all children before any enter_tree

	if (data.parent) {
		data.tree = data.parent->data.tree;
		data.depth = data.parent->data.depth + 1;
	} else {

		data.depth = 1;
	}

	data.viewport = Object::cast_to<Viewport>(this);
	if (!data.viewport)
		data.viewport = data.parent->data.viewport;

	data.inside_tree = true;

	for (Map<StringName, GroupData>::Element *E = data.grouped.front(); E; E = E->next()) {
		E->get().group = data.tree->add_to_group(E->key(), this);
	}

	notification(NOTIFICATION_ENTER_TREE);

	if (get_script_instance()) {

		get_script_instance()->call_multilevel_reversed(SceneStringNames::get_singleton()->_enter_tree, NULL, 0);
	}

	emit_signal(SceneStringNames::get_singleton()->tree_entered);

	data.tree->node_added(this);

	data.blocked++;
	//block while adding children

	for (int i = 0; i < data.children.size(); i++) {

		if (!data.children[i]->is_inside_tree()) // could have been added in enter_tree
			data.children[i]->_propagate_enter_tree();
	}

	data.blocked--;
	// enter groups
}

void Node::_propagate_exit_tree() {

	data.blocked++;

	for (int i = data.children.size() - 1; i >= 0; i--) {

		data.children[i]->_propagate_exit_tree();
	}

	data.blocked--;

	if (get_script_instance()) {

		get_script_instance()->call_multilevel(SceneStringNames::get_singleton()->_exit_tree, NULL, 0);
	}
	emit_signal(SceneStringNames::get_singleton()->tree_exiting);

	notification(NOTIFICATION_EXIT_TREE, true);
	if (data.tree)
		data.tree->node_removed(this);

	// exit groups

	for (Map<StringName, GroupData>::Element *E = data.grouped.front(); E; E = E->next()) {
		data.tree->remove_from_group(E->key(), this);
		E->get().group = NULL;
	}

	data.viewport = NULL;

	if (data.tree)
		data.tree->tree_changed();

	data.inside_tree = false;
	data.ready_notified = false;
	data.tree = NULL;
	data.depth = -1;

	emit_signal(SceneStringNames::get_singleton()->tree_exited);
}

void Node::move_child(Node *p_child, int p_pos) {

	ERR_FAIL_NULL(p_child);
	ERR_EXPLAIN("Invalid new child position: " + itos(p_pos));
	ERR_FAIL_INDEX(p_pos, data.children.size() + 1);
	ERR_EXPLAIN("child is not a child of this node.");
	ERR_FAIL_COND(p_child->data.parent != this);
	if (data.blocked > 0) {
		ERR_EXPLAIN("Parent node is busy setting up children, move_child() failed. Consider using call_deferred(\"move_child\") instead (or \"popup\" if this is from a popup).");
		ERR_FAIL_COND(data.blocked > 0);
	}

	// Specifying one place beyond the end
	// means the same as moving to the last position
	if (p_pos == data.children.size())
		p_pos--;

	if (p_child->data.pos == p_pos)
		return; //do nothing

	int motion_from = MIN(p_pos, p_child->data.pos);
	int motion_to = MAX(p_pos, p_child->data.pos);

	data.children.remove(p_child->data.pos);
	data.children.insert(p_pos, p_child);

	if (data.tree) {
		data.tree->tree_changed();
	}

	data.blocked++;
	//new pos first
	for (int i = motion_from; i <= motion_to; i++) {

		data.children[i]->data.pos = i;
	}
	// notification second
	move_child_notify(p_child);
	for (int i = motion_from; i <= motion_to; i++) {
		data.children[i]->notification(NOTIFICATION_MOVED_IN_PARENT);
	}
	for (const Map<StringName, GroupData>::Element *E = p_child->data.grouped.front(); E; E = E->next()) {
		if (E->get().group)
			E->get().group->changed = true;
	}

	data.blocked--;
}

void Node::raise() {

	if (!data.parent)
		return;

	data.parent->move_child(this, data.parent->data.children.size() - 1);
}

void Node::add_child_notify(Node *p_child) {

	// to be used when not wanted
}

void Node::remove_child_notify(Node *p_child) {

	// to be used when not wanted
}

void Node::move_child_notify(Node *p_child) {

	// to be used when not wanted
}

void Node::set_fixed_process(bool p_process) {

	if (data.fixed_process == p_process)
		return;

	data.fixed_process = p_process;

	if (data.fixed_process)
		add_to_group("fixed_process", false);
	else
		remove_from_group("fixed_process");

	data.fixed_process = p_process;
	_change_notify("fixed_process");
}

bool Node::is_fixed_processing() const {

	return data.fixed_process;
}

void Node::set_fixed_process_internal(bool p_process_internal) {

	if (data.fixed_process_internal == p_process_internal)
		return;

	data.fixed_process_internal = p_process_internal;

	if (data.fixed_process_internal)
		add_to_group("fixed_process_internal", false);
	else
		remove_from_group("fixed_process_internal");

	data.fixed_process_internal = p_process_internal;
	_change_notify("fixed_process_internal");
}

bool Node::is_fixed_processing_internal() const {

	return data.fixed_process_internal;
}

void Node::set_pause_mode(PauseMode p_mode) {

	if (data.pause_mode == p_mode)
		return;

	bool prev_inherits = data.pause_mode == PAUSE_MODE_INHERIT;
	data.pause_mode = p_mode;
	if (!is_inside_tree())
		return; //pointless
	if ((data.pause_mode == PAUSE_MODE_INHERIT) == prev_inherits)
		return; ///nothing changed

	Node *owner = NULL;

	if (data.pause_mode == PAUSE_MODE_INHERIT) {

		if (data.parent)
			owner = data.parent->data.pause_owner;
	} else {
		owner = this;
	}

	_propagate_pause_owner(owner);
}

Node::PauseMode Node::get_pause_mode() const {

	return data.pause_mode;
}

void Node::_propagate_pause_owner(Node *p_owner) {

	if (this != p_owner && data.pause_mode != PAUSE_MODE_INHERIT)
		return;
	data.pause_owner = p_owner;
	for (int i = 0; i < data.children.size(); i++) {

		data.children[i]->_propagate_pause_owner(p_owner);
	}
}

bool Node::can_process() const {

	ERR_FAIL_COND_V(!is_inside_tree(), false);

	if (get_tree()->is_paused()) {

		if (data.pause_mode == PAUSE_MODE_STOP)
			return false;
		if (data.pause_mode == PAUSE_MODE_PROCESS)
			return true;
		if (data.pause_mode == PAUSE_MODE_INHERIT) {

			if (!data.pause_owner)
				return false; //clearly no pause owner by default

			if (data.pause_owner->data.pause_mode == PAUSE_MODE_PROCESS)
				return true;

			if (data.pause_owner->data.pause_mode == PAUSE_MODE_STOP)
				return false;
		}
	}

	return true;
}

float Node::get_fixed_process_delta_time() const {

	if (data.tree)
		return data.tree->get_fixed_process_time();
	else
		return 0;
}

float Node::get_process_delta_time() const {

	if (data.tree)
		return data.tree->get_idle_process_time();
	else
		return 0;
}

void Node::set_process(bool p_idle_process) {

	if (data.idle_process == p_idle_process)
		return;

	data.idle_process = p_idle_process;

	if (data.idle_process)
		add_to_group("idle_process", false);
	else
		remove_from_group("idle_process");

	data.idle_process = p_idle_process;
	_change_notify("idle_process");
}

bool Node::is_processing() const {

	return data.idle_process;
}

void Node::set_process_internal(bool p_idle_process_internal) {

	if (data.idle_process_internal == p_idle_process_internal)
		return;

	data.idle_process_internal = p_idle_process_internal;

	if (data.idle_process_internal)
		add_to_group("idle_process_internal", false);
	else
		remove_from_group("idle_process_internal");

	data.idle_process_internal = p_idle_process_internal;
	_change_notify("idle_process_internal");
}

bool Node::is_processing_internal() const {

	return data.idle_process_internal;
}

void Node::set_process_input(bool p_enable) {

	if (p_enable == data.input)
		return;

	data.input = p_enable;
	if (!is_inside_tree())
		return;

	if (p_enable)
		add_to_group("_vp_input" + itos(get_viewport()->get_instance_id()));
	else
		remove_from_group("_vp_input" + itos(get_viewport()->get_instance_id()));
}

bool Node::is_processing_input() const {
	return data.input;
}

void Node::set_process_unhandled_input(bool p_enable) {

	if (p_enable == data.unhandled_input)
		return;
	data.unhandled_input = p_enable;
	if (!is_inside_tree())
		return;

	if (p_enable)
		add_to_group("_vp_unhandled_input" + itos(get_viewport()->get_instance_id()));
	else
		remove_from_group("_vp_unhandled_input" + itos(get_viewport()->get_instance_id()));
}

bool Node::is_processing_unhandled_input() const {
	return data.unhandled_input;
}

void Node::set_process_unhandled_key_input(bool p_enable) {

	if (p_enable == data.unhandled_key_input)
		return;
	data.unhandled_key_input = p_enable;
	if (!is_inside_tree())
		return;

	if (p_enable)
		add_to_group("_vp_unhandled_key_input" + itos(get_viewport()->get_instance_id()));
	else
		remove_from_group("_vp_unhandled_key_input" + itos(get_viewport()->get_instance_id()));
}

bool Node::is_processing_unhandled_key_input() const {
	return data.unhandled_key_input;
}

StringName Node::get_name() const {

	return data.name;
}

void Node::_set_name_nocheck(const StringName &p_name) {

	data.name = p_name;
}

void Node::set_name(const String &p_name) {

	String name = p_name.replace(":", "").replace("/", "").replace("@", "");

	ERR_FAIL_COND(name == "");
	data.name = name;

	if (data.parent) {

		data.parent->_validate_child_name(this);
	}

	propagate_notification(NOTIFICATION_PATH_CHANGED);

	if (is_inside_tree()) {

		emit_signal("renamed");
		get_tree()->tree_changed();
	}
}

static bool node_hrcr = false;
static SafeRefCount node_hrcr_count;

void Node::init_node_hrcr() {
	node_hrcr_count.init(1);
}

void Node::set_human_readable_collision_renaming(bool p_enabled) {

	node_hrcr = p_enabled;
}

void Node::_validate_child_name(Node *p_child, bool p_force_human_readable) {

	/* Make sure the name is unique */

	if (node_hrcr || p_force_human_readable) {

		//this approach to autoset node names is human readable but very slow
		//it's turned on while running in the editor

		p_child->data.name = _generate_serial_child_name(p_child);

	} else {

		//this approach to autoset node names is fast but not as readable
		//it's the default and reserves the '@' character for unique names.

		bool unique = true;

		if (p_child->data.name == StringName() || p_child->data.name.operator String()[0] == '@') {
			//new unique name must be assigned
			unique = false;
		} else {
			//check if exists
			Node **children = data.children.ptrw();
			int cc = data.children.size();

			for (int i = 0; i < cc; i++) {
				if (children[i] == p_child)
					continue;
				if (children[i]->data.name == p_child->data.name) {
					unique = false;
					break;
				}
			}
		}

		if (!unique) {

			node_hrcr_count.ref();
			String name = "@" + String(p_child->get_name()) + "@" + itos(node_hrcr_count.get());
			p_child->data.name = name;
		}
	}
}

String Node::_generate_serial_child_name(Node *p_child) {

	String name = p_child->data.name;

	if (name == "") {

		name = p_child->get_class();
		// Adjust casing according to project setting. The current type name is expected to be in PascalCase.
		switch (ProjectSettings::get_singleton()->get("node/name_casing").operator int()) {
			case NAME_CASING_PASCAL_CASE:
				break;
			case NAME_CASING_CAMEL_CASE:
				name[0] = name.to_lower()[0];
				break;
			case NAME_CASING_SNAKE_CASE:
				name = name.camelcase_to_underscore(true);
				break;
		}
	}

	// Extract trailing number
	String nums;
	for (int i = name.length() - 1; i >= 0; i--) {
		CharType n = name[i];
		if (n >= '0' && n <= '9') {
			nums = String::chr(name[i]) + nums;
		} else {
			break;
		}
	}

	String nnsep = _get_name_num_separator();
	int num = 0;
	bool explicit_zero = false;
	if (nums.length() > 0 && name.substr(name.length() - nnsep.length() - nums.length(), nnsep.length()) == nnsep) {
		// Base name + Separator + Number
		num = nums.to_int();
		name = name.substr(0, name.length() - nnsep.length() - nums.length()); // Keep base name
		if (num == 0) {
			explicit_zero = true;
		}
	}

	int num_places = nums.length();
	for (;;) {
		String attempt = (name + (num > 0 || explicit_zero ? nnsep + itos(num).pad_zeros(num_places) : "")).strip_edges();
		bool found = false;
		for (int i = 0; i < data.children.size(); i++) {
			if (data.children[i] == p_child)
				continue;
			if (data.children[i]->data.name == attempt) {
				found = true;
				break;
			}
		}
		if (!found) {
			return attempt;
		} else {
			if (num == 0) {
				if (explicit_zero) {
					// Name ended in separator + 0; user expects to get to separator + 1
					num = 1;
				} else {
					// Name was undecorated so skip to 2 for a more natural result
					num = 2;
				}
			} else {
				num++;
			}
		}
	}
}

void Node::_add_child_nocheck(Node *p_child, const StringName &p_name) {
	//add a child node quickly, without name validation

	p_child->data.name = p_name;
	p_child->data.pos = data.children.size();
	data.children.push_back(p_child);
	p_child->data.parent = this;
	p_child->notification(NOTIFICATION_PARENTED);

	if (data.tree) {
		p_child->_set_tree(data.tree);
	}

	/* Notify */
	//recognize children created in this node constructor
	p_child->data.parent_owned = data.in_constructor;
	add_child_notify(p_child);
}

void Node::add_child(Node *p_child, bool p_legible_unique_name) {

	ERR_FAIL_NULL(p_child);

	if (p_child == this) {
		ERR_EXPLAIN("Can't add child '" + p_child->get_name() + "' to itself.")
		ERR_FAIL_COND(p_child == this); // adding to itself!
	}

	/* Fail if node has a parent */
	if (p_child->data.parent) {
		ERR_EXPLAIN("Can't add child '" + p_child->get_name() + "' to '" + get_name() + "', already has a parent '" + p_child->data.parent->get_name() + "'.");
		ERR_FAIL_COND(p_child->data.parent);
	}

	if (data.blocked > 0) {
		ERR_EXPLAIN("Parent node is busy setting up children, add_node() failed. Consider using call_deferred(\"add_child\", child) instead.");
		ERR_FAIL_COND(data.blocked > 0);
	}

	ERR_EXPLAIN("Can't add child while a notification is happening.");
	ERR_FAIL_COND(data.blocked > 0);

	/* Validate name */
	_validate_child_name(p_child, p_legible_unique_name);

	_add_child_nocheck(p_child, p_child->data.name);
}

void Node::add_child_below_node(Node *p_node, Node *p_child, bool p_legible_unique_name) {
	add_child(p_child, p_legible_unique_name);

	if (is_a_parent_of(p_node)) {
		move_child(p_child, p_node->get_position_in_parent() + 1);
	} else {
		WARN_PRINTS("Cannot move under node " + p_node->get_name() + " as " + p_child->get_name() + " does not share a parent.")
	}
}

void Node::_propagate_validate_owner() {

	if (data.owner) {

		bool found = false;
		Node *parent = data.parent;

		while (parent) {

			if (parent == data.owner) {

				found = true;
				break;
			}

			parent = parent->data.parent;
		}

		if (!found) {

			data.owner->data.owned.erase(data.OW);
			data.owner = NULL;
		}
	}

	for (int i = 0; i < data.children.size(); i++) {

		data.children[i]->_propagate_validate_owner();
	}
}

void Node::remove_child(Node *p_child) {

	ERR_FAIL_NULL(p_child);
	if (data.blocked > 0) {
		ERR_EXPLAIN("Parent node is busy setting up children, remove_node() failed. Consider using call_deferred(\"remove_child\",child) instead.");
		ERR_FAIL_COND(data.blocked > 0);
	}

	int idx = -1;
	for (int i = 0; i < data.children.size(); i++) {

		if (data.children[i] == p_child) {

			idx = i;
			break;
		}
	}

	ERR_FAIL_COND(idx == -1);
	//ERR_FAIL_COND( p_child->data.blocked > 0 );

	//if (data.scene) { does not matter

	p_child->_set_tree(NULL);
	//}

	remove_child_notify(p_child);
	p_child->notification(NOTIFICATION_UNPARENTED);

	data.children.remove(idx);

	for (int i = idx; i < data.children.size(); i++) {

		data.children[i]->data.pos = i;
	}

	p_child->data.parent = NULL;
	p_child->data.pos = -1;

	// validate owner
	p_child->_propagate_validate_owner();
}

int Node::get_child_count() const {

	return data.children.size();
}
Node *Node::get_child(int p_index) const {

	ERR_FAIL_INDEX_V(p_index, data.children.size(), NULL);

	return data.children[p_index];
}

Node *Node::_get_child_by_name(const StringName &p_name) const {

	int cc = data.children.size();
	Node *const *cd = data.children.ptr();

	for (int i = 0; i < cc; i++) {
		if (cd[i]->data.name == p_name)
			return cd[i];
	}

	return NULL;
}

Node *Node::_get_node(const NodePath &p_path) const {

	if (!data.inside_tree && p_path.is_absolute()) {
		ERR_EXPLAIN("Can't use get_node() with absolute paths from outside the active scene tree.");
		ERR_FAIL_V(NULL);
	}

	Node *current = NULL;
	Node *root = NULL;

	if (!p_path.is_absolute()) {
		current = const_cast<Node *>(this); //start from this
	} else {

		root = const_cast<Node *>(this);
		while (root->data.parent)
			root = root->data.parent; //start from root
	}

	for (int i = 0; i < p_path.get_name_count(); i++) {

		StringName name = p_path.get_name(i);
		Node *next = NULL;

		if (name == SceneStringNames::get_singleton()->dot) { // .

			next = current;

		} else if (name == SceneStringNames::get_singleton()->doubledot) { // ..

			if (current == NULL || !current->data.parent)
				return NULL;

			next = current->data.parent;
		} else if (current == NULL) {

			if (name == root->get_name())
				next = root;

		} else {

			next = NULL;

			for (int j = 0; j < current->data.children.size(); j++) {

				Node *child = current->data.children[j];

				if (child->data.name == name) {

					next = child;
					break;
				}
			}
			if (next == NULL) {
				return NULL;
			};
		}
		current = next;
	}

	return current;
}

Node *Node::get_node(const NodePath &p_path) const {

	Node *node = _get_node(p_path);
	if (!node) {
		ERR_EXPLAIN("Node not found: " + p_path);
		ERR_FAIL_COND_V(!node, NULL);
	}
	return node;
}

bool Node::has_node(const NodePath &p_path) const {

	return _get_node(p_path) != NULL;
}

Node *Node::find_node(const String &p_mask, bool p_recursive, bool p_owned) const {

	Node *const *cptr = data.children.ptr();
	int ccount = data.children.size();
	for (int i = 0; i < ccount; i++) {
		if (p_owned && !cptr[i]->data.owner)
			continue;
		if (cptr[i]->data.name.operator String().match(p_mask))
			return cptr[i];

		if (!p_recursive)
			continue;

		Node *ret = cptr[i]->find_node(p_mask, true, p_owned);
		if (ret)
			return ret;
	}
	return NULL;
}

Node *Node::get_parent() const {

	return data.parent;
}

bool Node::is_a_parent_of(const Node *p_node) const {

	ERR_FAIL_NULL_V(p_node, false);
	Node *p = p_node->data.parent;
	while (p) {

		if (p == this)
			return true;
		p = p->data.parent;
	}

	return false;
}

bool Node::is_greater_than(const Node *p_node) const {

	ERR_FAIL_NULL_V(p_node, false);
	ERR_FAIL_COND_V(!data.inside_tree, false);
	ERR_FAIL_COND_V(!p_node->data.inside_tree, false);

	ERR_FAIL_COND_V(data.depth < 0, false);
	ERR_FAIL_COND_V(p_node->data.depth < 0, false);
#ifdef NO_ALLOCA

	Vector<int> this_stack;
	Vector<int> that_stack;
	this_stack.resize(data.depth);
	that_stack.resize(p_node->data.depth);

#else

	int *this_stack = (int *)alloca(sizeof(int) * data.depth);
	int *that_stack = (int *)alloca(sizeof(int) * p_node->data.depth);

#endif

	const Node *n = this;

	int idx = data.depth - 1;
	while (n) {
		ERR_FAIL_INDEX_V(idx, data.depth, false);
		this_stack[idx--] = n->data.pos;
		n = n->data.parent;
	}
	ERR_FAIL_COND_V(idx != -1, false);
	n = p_node;
	idx = p_node->data.depth - 1;
	while (n) {
		ERR_FAIL_INDEX_V(idx, p_node->data.depth, false);
		that_stack[idx--] = n->data.pos;

		n = n->data.parent;
	}
	ERR_FAIL_COND_V(idx != -1, false);
	idx = 0;

	bool res;
	while (true) {

		// using -2 since out-of-tree or nonroot nodes have -1
		int this_idx = (idx >= data.depth) ? -2 : this_stack[idx];
		int that_idx = (idx >= p_node->data.depth) ? -2 : that_stack[idx];

		if (this_idx > that_idx) {
			res = true;
			break;
		} else if (this_idx < that_idx) {
			res = false;
			break;
		} else if (this_idx == -2) {
			res = false; // equal
			break;
		}
		idx++;
	}

	return res;
}

void Node::get_owned_by(Node *p_by, List<Node *> *p_owned) {

	if (data.owner == p_by)
		p_owned->push_back(this);

	for (int i = 0; i < get_child_count(); i++)
		get_child(i)->get_owned_by(p_by, p_owned);
}

void Node::_set_owner_nocheck(Node *p_owner) {

	if (data.owner == p_owner)
		return;

	ERR_FAIL_COND(data.owner);
	data.owner = p_owner;
	data.owner->data.owned.push_back(this);
	data.OW = data.owner->data.owned.back();
}

void Node::set_owner(Node *p_owner) {

	if (data.owner) {

		data.owner->data.owned.erase(data.OW);
		data.OW = NULL;
		data.owner = NULL;
	}

	ERR_FAIL_COND(p_owner == this);

	if (!p_owner)
		return;

	Node *check = this->get_parent();
	bool owner_valid = false;

	while (check) {

		if (check == p_owner) {
			owner_valid = true;
			break;
		}

		check = check->data.parent;
	}

	ERR_FAIL_COND(!owner_valid);

	_set_owner_nocheck(p_owner);
}
Node *Node::get_owner() const {

	return data.owner;
}

Node *Node::find_common_parent_with(const Node *p_node) const {

	if (this == p_node)
		return const_cast<Node *>(p_node);

	Set<const Node *> visited;

	const Node *n = this;

	while (n) {

		visited.insert(n);
		n = n->data.parent;
	}

	const Node *common_parent = p_node;

	while (common_parent) {

		if (visited.has(common_parent))
			break;
		common_parent = common_parent->data.parent;
	}

	if (!common_parent)
		return NULL;

	return const_cast<Node *>(common_parent);
}

NodePath Node::get_path_to(const Node *p_node) const {

	ERR_FAIL_NULL_V(p_node, NodePath());

	if (this == p_node)
		return NodePath(".");

	Set<const Node *> visited;

	const Node *n = this;

	while (n) {

		visited.insert(n);
		n = n->data.parent;
	}

	const Node *common_parent = p_node;

	while (common_parent) {

		if (visited.has(common_parent))
			break;
		common_parent = common_parent->data.parent;
	}

	ERR_FAIL_COND_V(!common_parent, NodePath()); //nodes not in the same tree

	visited.clear();

	Vector<StringName> path;

	n = p_node;

	while (n != common_parent) {

		path.push_back(n->get_name());
		n = n->data.parent;
	}

	n = this;
	StringName up = String("..");

	while (n != common_parent) {

		path.push_back(up);
		n = n->data.parent;
	}

	path.invert();

	return NodePath(path, false);
}

NodePath Node::get_path() const {

	ERR_FAIL_COND_V(!is_inside_tree(), NodePath());

	if (data.path_cache)
		return *data.path_cache;

	const Node *n = this;

	Vector<StringName> path;

	while (n) {
		path.push_back(n->get_name());
		n = n->data.parent;
	}

	path.invert();

	data.path_cache = memnew(NodePath(path, true));

	return *data.path_cache;
}

bool Node::is_in_group(const StringName &p_identifier) const {

	return data.grouped.has(p_identifier);
}

void Node::add_to_group(const StringName &p_identifier, bool p_persistent) {

	ERR_FAIL_COND(!p_identifier.operator String().length());

	if (data.grouped.has(p_identifier))
		return;

	GroupData gd;

	if (data.tree) {
		gd.group = data.tree->add_to_group(p_identifier, this);
	} else {
		gd.group = NULL;
	}

	gd.persistent = p_persistent;

	data.grouped[p_identifier] = gd;
}

void Node::remove_from_group(const StringName &p_identifier) {

	ERR_FAIL_COND(!data.grouped.has(p_identifier));

	Map<StringName, GroupData>::Element *E = data.grouped.find(p_identifier);

	ERR_FAIL_COND(!E);

	if (data.tree)
		data.tree->remove_from_group(E->key(), this);

	data.grouped.erase(E);
}

Array Node::_get_groups() const {

	Array groups;
	List<GroupInfo> gi;
	get_groups(&gi);
	for (List<GroupInfo>::Element *E = gi.front(); E; E = E->next()) {
		groups.push_back(E->get().name);
	}

	return groups;
}

void Node::get_groups(List<GroupInfo> *p_groups) const {

	for (const Map<StringName, GroupData>::Element *E = data.grouped.front(); E; E = E->next()) {
		GroupInfo gi;
		gi.name = E->key();
		gi.persistent = E->get().persistent;
		p_groups->push_back(gi);
	}
}

bool Node::has_persistent_groups() const {

	for (const Map<StringName, GroupData>::Element *E = data.grouped.front(); E; E = E->next()) {
		if (E->get().persistent)
			return true;
	}

	return false;
}
void Node::_print_tree(const Node *p_node) {

	print_line(String(p_node->get_path_to(this)));
	for (int i = 0; i < data.children.size(); i++)
		data.children[i]->_print_tree(p_node);
}

void Node::print_tree() {

	_print_tree(this);
}

void Node::_propagate_reverse_notification(int p_notification) {

	data.blocked++;
	for (int i = data.children.size() - 1; i >= 0; i--) {

		data.children[i]->_propagate_reverse_notification(p_notification);
	}

	notification(p_notification, true);
	data.blocked--;
}

void Node::_propagate_deferred_notification(int p_notification, bool p_reverse) {

	ERR_FAIL_COND(!is_inside_tree());

	data.blocked++;

	if (!p_reverse)
		MessageQueue::get_singleton()->push_notification(this, p_notification);

	for (int i = 0; i < data.children.size(); i++) {

		data.children[i]->_propagate_deferred_notification(p_notification, p_reverse);
	}

	if (p_reverse)
		MessageQueue::get_singleton()->push_notification(this, p_notification);

	data.blocked--;
}

void Node::propagate_notification(int p_notification) {

	data.blocked++;
	notification(p_notification);

	for (int i = 0; i < data.children.size(); i++) {

		data.children[i]->propagate_notification(p_notification);
	}
	data.blocked--;
}

void Node::propagate_call(const StringName &p_method, const Array &p_args, const bool p_parent_first) {

	data.blocked++;

	if (p_parent_first && has_method(p_method))
		callv(p_method, p_args);

	for (int i = 0; i < data.children.size(); i++) {
		data.children[i]->propagate_call(p_method, p_args, p_parent_first);
	}

	if (!p_parent_first && has_method(p_method))
		callv(p_method, p_args);

	data.blocked--;
}

void Node::_propagate_replace_owner(Node *p_owner, Node *p_by_owner) {
	if (get_owner() == p_owner)
		set_owner(p_by_owner);

	data.blocked++;
	for (int i = 0; i < data.children.size(); i++)
		data.children[i]->_propagate_replace_owner(p_owner, p_by_owner);
	data.blocked--;
}

int Node::get_index() const {

	return data.pos;
}
void Node::remove_and_skip() {

	ERR_FAIL_COND(!data.parent);

	Node *new_owner = get_owner();

	List<Node *> children;

	while (true) {

		bool clear = true;
		for (int i = 0; i < data.children.size(); i++) {
			Node *c_node = data.children[i];
			if (!c_node->get_owner())
				continue;

			remove_child(c_node);
			c_node->_propagate_replace_owner(this, NULL);
			children.push_back(c_node);
			clear = false;
			break;
		}

		if (clear)
			break;
	}

	while (!children.empty()) {

		Node *c_node = children.front()->get();
		data.parent->add_child(c_node);
		c_node->_propagate_replace_owner(NULL, new_owner);
		children.pop_front();
	}

	data.parent->remove_child(this);
}

void Node::set_filename(const String &p_filename) {

	data.filename = p_filename;
}
String Node::get_filename() const {

	return data.filename;
}

void Node::set_editable_instance(Node *p_node, bool p_editable) {

	ERR_FAIL_NULL(p_node);
	ERR_FAIL_COND(!is_a_parent_of(p_node));
	NodePath p = get_path_to(p_node);
	if (!p_editable) {
		data.editable_instances.erase(p);
		// Avoid this flag being needlessly saved;
		// also give more visual feedback if editable children is re-enabled
		set_display_folded(false);
	} else {
		data.editable_instances[p] = true;
	}
}

bool Node::is_editable_instance(Node *p_node) const {

	if (!p_node)
		return false; //easier, null is never editable :)
	ERR_FAIL_COND_V(!is_a_parent_of(p_node), false);
	NodePath p = get_path_to(p_node);
	return data.editable_instances.has(p);
}

void Node::set_editable_instances(const HashMap<NodePath, int> &p_editable_instances) {

	data.editable_instances = p_editable_instances;
}

HashMap<NodePath, int> Node::get_editable_instances() const {

	return data.editable_instances;
}

int Node::get_position_in_parent() const {

	return data.pos;
}

Node *Node::_duplicate(int p_flags, Map<const Node *, Node *> *r_duplimap) const {

	Node *node = NULL;

	Object *obj = ClassDB::instance(get_class());
	ERR_FAIL_COND_V(!obj, NULL);
	node = Object::cast_to<Node>(obj);
	if (!node)
		memdelete(obj);
	ERR_FAIL_COND_V(!node, NULL);

	if (get_filename() != "") { //an instance
		node->set_filename(get_filename());
	}

	StringName script_property_name = CoreStringNames::get_singleton()->_script;

	List<const Node *> hidden_roots;
	List<const Node *> node_tree;
	node_tree.push_front(this);

	for (List<const Node *>::Element *N = node_tree.front(); N; N = N->next()) {

		Node *current_node = node->get_node(get_path_to(N->get()));
		ERR_CONTINUE(!current_node);

		if (p_flags & DUPLICATE_SCRIPTS) {
			bool is_valid = false;
			Variant script = N->get()->get(script_property_name, &is_valid);
			if (is_valid) {
				current_node->set(script_property_name, script);
			}
		}

		List<PropertyInfo> plist;
		N->get()->get_property_list(&plist);

		for (List<PropertyInfo>::Element *E = plist.front(); E; E = E->next()) {

			if (!(E->get().usage & PROPERTY_USAGE_STORAGE))
				continue;
			String name = E->get().name;
			if (name == script_property_name)
				continue;

			Variant value = N->get()->get(name);
			// Duplicate dictionaries and arrays, mainly needed for __meta__
			if (value.get_type() == Variant::DICTIONARY) {
				value = Dictionary(value).duplicate();
			} else if (value.get_type() == Variant::ARRAY) {
				value = Array(value).duplicate();
			}

			if (E->get().usage & PROPERTY_USAGE_DO_NOT_SHARE_ON_DUPLICATE) {

				Resource *res = Object::cast_to<Resource>(value);
				if (res) // Duplicate only if it's a resource
					current_node->set(name, res->duplicate());

			} else {

				current_node->set(name, value);
			}
		}
	}

	node->set_name(get_name());

	if (p_flags & DUPLICATE_GROUPS) {
		List<GroupInfo> gi;
		get_groups(&gi);
		for (List<GroupInfo>::Element *E = gi.front(); E; E = E->next()) {

			node->add_to_group(E->get().name, E->get().persistent);
		}
	}

	for (int i = 0; i < get_child_count(); i++) {

		if (get_child(i)->data.parent_owned)
			continue;

		Node *dup = get_child(i)->_duplicate(p_flags, r_duplimap);
		if (!dup) {

			memdelete(node);
			return NULL;
		}

		node->add_child(dup);
		if (i < node->get_child_count() - 1) {
			node->move_child(dup, i);
		}
	}

	for (List<const Node *>::Element *E = hidden_roots.front(); E; E = E->next()) {

		Node *parent = node->get_node(get_path_to(E->get()->data.parent));
		if (!parent) {

			memdelete(node);
			return NULL;
		}

		Node *dup = E->get()->_duplicate(p_flags, r_duplimap);
		if (!dup) {

			memdelete(node);
			return NULL;
		}

		parent->add_child(dup);
		int pos = E->get()->get_position_in_parent();

		if (pos < parent->get_child_count() - 1) {

			parent->move_child(dup, pos);
		}
	}

	return node;
}

Node *Node::duplicate(int p_flags) const {

	Node *dupe = _duplicate(p_flags);

	if (dupe && (p_flags & DUPLICATE_SIGNALS)) {
		_duplicate_signals(this, dupe);
	}

	return dupe;
}

void Node::_duplicate_and_reown(Node *p_new_parent, const Map<Node *, Node *> &p_reown_map) const {

	if (get_owner() != get_parent()->get_owner())
		return;

	Node *node = NULL;

	Object *obj = ClassDB::instance(get_class());
	if (!obj) {
		print_line("could not duplicate: " + String(get_class()));
	}
	ERR_FAIL_COND(!obj);
	node = Object::cast_to<Node>(obj);
	if (!node)
		memdelete(obj);

	List<PropertyInfo> plist;

	get_property_list(&plist);

	for (List<PropertyInfo>::Element *E = plist.front(); E; E = E->next()) {

		if (!(E->get().usage & PROPERTY_USAGE_STORAGE))
			continue;
		String name = E->get().name;

		Variant value = get(name);
		// Duplicate dictionaries and arrays, mainly needed for __meta__
		if (value.get_type() == Variant::DICTIONARY) {
			value = Dictionary(value).duplicate();
		} else if (value.get_type() == Variant::ARRAY) {
			value = Array(value).duplicate();
		}

		node->set(name, value);
	}

	node->set_name(get_name());
	p_new_parent->add_child(node);

	Node *owner = get_owner();

	if (p_reown_map.has(owner))
		owner = p_reown_map[owner];

	if (owner) {
		NodePath p = get_path_to(owner);
		if (owner != this) {
			Node *new_owner = node->get_node(p);
			if (new_owner) {
				node->set_owner(new_owner);
			}
		}
	}

	for (int i = 0; i < get_child_count(); i++) {

		get_child(i)->_duplicate_and_reown(node, p_reown_map);
	}
}

// Duplication of signals must happen after all the node descendants have been copied,
// because re-targeting of connections from some descendant to another is not possible
// if the emitter node comes later in tree order than the receiver
void Node::_duplicate_signals(const Node *p_original, Node *p_copy) const {

	if (this != p_original && (get_owner() != p_original && get_owner() != p_original->get_owner()))
		return;

	List<Connection> conns;
	get_all_signal_connections(&conns);

	for (List<Connection>::Element *E = conns.front(); E; E = E->next()) {

		if (E->get().flags & CONNECT_PERSIST) {
			//user connected
			NodePath p = p_original->get_path_to(this);
			Node *copy = p_copy->get_node(p);

			Node *target = Object::cast_to<Node>(E->get().target);
			if (!target) {
				continue;
			}
			NodePath ptarget = p_original->get_path_to(target);
			Node *copytarget = p_copy->get_node(ptarget);

			// Cannot find a path to the duplicate target, so it seems it's not part
			// of the duplicated and not yet parented hierarchy, so at least try to connect
			// to the same target as the original
			if (!copytarget)
				copytarget = target;

			if (copy && copytarget) {
				copy->connect(E->get().signal, copytarget, E->get().method, E->get().binds, E->get().flags);
			}
		}
	}

	for (int i = 0; i < get_child_count(); i++) {
		get_child(i)->_duplicate_signals(p_original, p_copy);
	}
}

Node *Node::duplicate_and_reown(const Map<Node *, Node *> &p_reown_map) const {

	ERR_FAIL_COND_V(get_filename() != "", NULL);

	Node *node = NULL;

	Object *obj = ClassDB::instance(get_class());
	if (!obj) {
		print_line("could not duplicate: " + String(get_class()));
	}
	ERR_FAIL_COND_V(!obj, NULL);
	node = Object::cast_to<Node>(obj);
	if (!node)
		memdelete(obj);
	ERR_FAIL_COND_V(!node, NULL);

	node->set_name(get_name());

	List<PropertyInfo> plist;

	get_property_list(&plist);

	for (List<PropertyInfo>::Element *E = plist.front(); E; E = E->next()) {

		if (!(E->get().usage & PROPERTY_USAGE_STORAGE))
			continue;
		String name = E->get().name;
		node->set(name, get(name));
	}

	for (int i = 0; i < get_child_count(); i++) {

		get_child(i)->_duplicate_and_reown(node, p_reown_map);
	}

	// Duplication of signals must happen after all the node descendants have been copied,
	// because re-targeting of connections from some descendant to another is not possible
	// if the emitter node comes later in tree order than the receiver
	_duplicate_signals(this, node);
	return node;
}

static void find_owned_by(Node *p_by, Node *p_node, List<Node *> *p_owned) {

	if (p_node->get_owner() == p_by)
		p_owned->push_back(p_node);

	for (int i = 0; i < p_node->get_child_count(); i++) {

		find_owned_by(p_by, p_node->get_child(i), p_owned);
	}
}

struct _NodeReplaceByPair {

	String name;
	Variant value;
};

void Node::replace_by(Node *p_node, bool p_keep_data) {

	ERR_FAIL_NULL(p_node);
	ERR_FAIL_COND(p_node->data.parent);

	List<Node *> owned = data.owned;
	List<Node *> owned_by_owner;
	Node *owner = (data.owner == this) ? p_node : data.owner;

	List<_NodeReplaceByPair> replace_data;

	if (p_keep_data) {

		List<PropertyInfo> plist;
		get_property_list(&plist);

		for (List<PropertyInfo>::Element *E = plist.front(); E; E = E->next()) {

			_NodeReplaceByPair rd;
			if (!(E->get().usage & PROPERTY_USAGE_STORAGE))
				continue;
			rd.name = E->get().name;
			rd.value = get(rd.name);
		}

		List<GroupInfo> groups;
		get_groups(&groups);

		for (List<GroupInfo>::Element *E = groups.front(); E; E = E->next())
			p_node->add_to_group(E->get().name, E->get().persistent);
	}

	_replace_connections_target(p_node);

	if (data.owner) {
		for (int i = 0; i < get_child_count(); i++)
			find_owned_by(data.owner, get_child(i), &owned_by_owner);
	}

	Node *parent = data.parent;
	int pos_in_parent = data.pos;

	if (data.parent) {

		parent->remove_child(this);
		parent->add_child(p_node);
		parent->move_child(p_node, pos_in_parent);
	}

	while (get_child_count()) {

		Node *child = get_child(0);
		remove_child(child);
		p_node->add_child(child);
	}

	p_node->set_owner(owner);
	for (int i = 0; i < owned.size(); i++)
		owned[i]->set_owner(p_node);

	for (int i = 0; i < owned_by_owner.size(); i++)
		owned_by_owner[i]->set_owner(owner);

	p_node->set_filename(get_filename());

	for (List<_NodeReplaceByPair>::Element *E = replace_data.front(); E; E = E->next()) {

		p_node->set(E->get().name, E->get().value);
	}
}

void Node::_replace_connections_target(Node *p_new_target) {

	List<Connection> cl;
	get_signals_connected_to_this(&cl);

	for (List<Connection>::Element *E = cl.front(); E; E = E->next()) {

		Connection &c = E->get();

		if (c.flags & CONNECT_PERSIST) {
			c.source->disconnect(c.signal, this, c.method);
			bool valid = p_new_target->has_method(c.method) || p_new_target->get_script().is_null() || Ref<Script>(p_new_target->get_script())->has_method(c.method);
			ERR_EXPLAIN("Attempt to connect signal \'" + c.source->get_class() + "." + c.signal + "\' to nonexistent method \'" + c.target->get_class() + "." + c.method + "\'");
			ERR_CONTINUE(!valid);
			c.source->connect(c.signal, p_new_target, c.method, c.binds, c.flags);
		}
	}
}

Vector<Variant> Node::make_binds(VARIANT_ARG_DECLARE) {

	Vector<Variant> ret;

	if (p_arg1.get_type() == Variant::NIL)
		return ret;
	else
		ret.push_back(p_arg1);

	if (p_arg2.get_type() == Variant::NIL)
		return ret;
	else
		ret.push_back(p_arg2);

	if (p_arg3.get_type() == Variant::NIL)
		return ret;
	else
		ret.push_back(p_arg3);

	if (p_arg4.get_type() == Variant::NIL)
		return ret;
	else
		ret.push_back(p_arg4);

	if (p_arg5.get_type() == Variant::NIL)
		return ret;
	else
		ret.push_back(p_arg5);

	return ret;
}

bool Node::has_node_and_resource(const NodePath &p_path) const {

	if (!has_node(p_path))
		return false;
	Node *node = get_node(p_path);

	bool result = false;

	node->get_indexed(p_path.get_subnames(), &result);

	return result;
}

Array Node::_get_node_and_resource(const NodePath &p_path) {

	Node *node;
	RES res;
	Vector<StringName> leftover_path;
	node = get_node_and_resource(p_path, res, leftover_path);
	Array result;

	if (node)
		result.push_back(node);
	else
		result.push_back(Variant());

	if (res.is_valid())
		result.push_back(res);
	else
		result.push_back(Variant());

	result.push_back(NodePath(Vector<StringName>(), leftover_path, false));

	return result;
}

Node *Node::get_node_and_resource(const NodePath &p_path, RES &r_res, Vector<StringName> &r_leftover_subpath, bool p_last_is_property) const {

	Node *node = get_node(p_path);
	r_res = RES();
	r_leftover_subpath = Vector<StringName>();
	if (!node)
		return NULL;

	if (p_path.get_subname_count()) {

		int j = 0;
		// If not p_last_is_property, we shouldn't consider the last one as part of the resource
		for (; j < p_path.get_subname_count() - p_last_is_property; j++) {
			RES new_res = j == 0 ? node->get(p_path.get_subname(j)) : r_res->get(p_path.get_subname(j));

			if (new_res.is_null()) {
				break;
			}

			r_res = new_res;
		}
		for (; j < p_path.get_subname_count(); j++) {
			// Put the rest of the subpath in the leftover path
			r_leftover_subpath.push_back(p_path.get_subname(j));
		}
	}

	return node;
}

void Node::_set_tree(SceneTree *p_tree) {

	SceneTree *tree_changed_a = NULL;
	SceneTree *tree_changed_b = NULL;

	//ERR_FAIL_COND(p_scene && data.parent && !data.parent->data.scene); //nobug if both are null

	if (data.tree) {
		_propagate_exit_tree();

		tree_changed_a = data.tree;
	}

	data.tree = p_tree;

	if (data.tree) {

		_propagate_enter_tree();
		if (!data.parent || data.parent->data.ready_notified) { // No parent (root) or parent ready
			_propagate_ready(); //reverse_notification(NOTIFICATION_READY);
		}

		tree_changed_b = data.tree;
	}

	if (tree_changed_a)
		tree_changed_a->tree_changed();
	if (tree_changed_b)
		tree_changed_b->tree_changed();
}

static void _Node_debug_sn(Object *p_obj) {

	Node *n = Object::cast_to<Node>(p_obj);
	if (!n)
		return;

	if (n->is_inside_tree())
		return;

	Node *p = n;
	while (p->get_parent()) {
		p = p->get_parent();
	}

	String path;
	if (p == n)
		path = n->get_name();
	else
		path = String(p->get_name()) + "/" + p->get_path_to(n);
	print_line(itos(p_obj->get_instance_id()) + "- Stray Node: " + path + " (Type: " + n->get_class() + ")");
}

void Node::_print_stray_nodes() {

	print_stray_nodes();
}

void Node::print_stray_nodes() {

#ifdef DEBUG_ENABLED

	ObjectDB::debug_objects(_Node_debug_sn);
#endif
}

void Node::queue_delete() {

	if (is_inside_tree()) {
		get_tree()->queue_delete(this);
	} else {
		SceneTree::get_singleton()->queue_delete(this);
	}
}

Array Node::_get_children() const {

	Array arr;
	int cc = get_child_count();
	arr.resize(cc);
	for (int i = 0; i < cc; i++)
		arr[i] = get_child(i);

	return arr;
}

static void _add_nodes_to_options(const Node *p_base, const Node *p_node, List<String> *r_options) {

	if (p_node != p_base && !p_node->get_owner())
		return;
	String n = p_base->get_path_to(p_node);
	r_options->push_back("\"" + n + "\"");
	for (int i = 0; i < p_node->get_child_count(); i++) {
		_add_nodes_to_options(p_base, p_node->get_child(i), r_options);
	}
}

void Node::get_argument_options(const StringName &p_function, int p_idx, List<String> *r_options) const {

	String pf = p_function;
	if ((pf == "has_node" || pf == "get_node") && p_idx == 0) {

		_add_nodes_to_options(this, this, r_options);
	}
	Object::get_argument_options(p_function, p_idx, r_options);
}

void Node::clear_internal_tree_resource_paths() {

	clear_internal_resource_paths();
	for (int i = 0; i < data.children.size(); i++) {
		data.children[i]->clear_internal_tree_resource_paths();
	}
}

String Node::get_configuration_warning() const {

	return String();
}

void Node::update_configuration_warning() {
}

bool Node::is_owned_by_parent() const {
	return data.parent_owned;
}

void Node::set_display_folded(bool p_folded) {
	data.display_folded = p_folded;
}

bool Node::is_displayed_folded() const {

	return data.display_folded;
}

void Node::request_ready() {
	data.ready_first = true;
}

void Node::_bind_methods() {

	GLOBAL_DEF("node/name_num_separator", 0);
	ProjectSettings::get_singleton()->set_custom_property_info("node/name_num_separator", PropertyInfo(Variant::INT, "node/name_num_separator", PROPERTY_HINT_ENUM, "None,Space,Underscore,Dash"));
	GLOBAL_DEF("node/name_casing", NAME_CASING_PASCAL_CASE);
	ProjectSettings::get_singleton()->set_custom_property_info("node/name_casing", PropertyInfo(Variant::INT, "node/name_casing", PROPERTY_HINT_ENUM, "PascalCase,camelCase,snake_case"));

	ClassDB::bind_method(D_METHOD("add_child_below_node", "node", "child_node", "legible_unique_name"), &Node::add_child_below_node, DEFVAL(false));

	ClassDB::bind_method(D_METHOD("set_name", "name"), &Node::set_name);
	ClassDB::bind_method(D_METHOD("get_name"), &Node::get_name);
	ClassDB::bind_method(D_METHOD("add_child", "node", "legible_unique_name"), &Node::add_child, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("remove_child", "node"), &Node::remove_child);
	ClassDB::bind_method(D_METHOD("get_child_count"), &Node::get_child_count);
	ClassDB::bind_method(D_METHOD("get_children"), &Node::_get_children);
	ClassDB::bind_method(D_METHOD("get_child", "idx"), &Node::get_child);
	ClassDB::bind_method(D_METHOD("has_node", "path"), &Node::has_node);
	ClassDB::bind_method(D_METHOD("get_node", "path"), &Node::get_node);
	ClassDB::bind_method(D_METHOD("get_parent"), &Node::get_parent);
	ClassDB::bind_method(D_METHOD("find_node", "mask", "recursive", "owned"), &Node::find_node, DEFVAL(true), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("has_node_and_resource", "path"), &Node::has_node_and_resource);
	ClassDB::bind_method(D_METHOD("get_node_and_resource", "path"), &Node::_get_node_and_resource);

	ClassDB::bind_method(D_METHOD("is_inside_tree"), &Node::is_inside_tree);
	ClassDB::bind_method(D_METHOD("is_a_parent_of", "node"), &Node::is_a_parent_of);
	ClassDB::bind_method(D_METHOD("is_greater_than", "node"), &Node::is_greater_than);
	ClassDB::bind_method(D_METHOD("get_path"), &Node::get_path);
	ClassDB::bind_method(D_METHOD("get_path_to", "node"), &Node::get_path_to);
	ClassDB::bind_method(D_METHOD("add_to_group", "group", "persistent"), &Node::add_to_group, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("remove_from_group", "group"), &Node::remove_from_group);
	ClassDB::bind_method(D_METHOD("is_in_group", "group"), &Node::is_in_group);
	ClassDB::bind_method(D_METHOD("move_child", "child_node", "to_position"), &Node::move_child);
	ClassDB::bind_method(D_METHOD("get_groups"), &Node::_get_groups);
	ClassDB::bind_method(D_METHOD("raise"), &Node::raise);
	ClassDB::bind_method(D_METHOD("set_owner", "owner"), &Node::set_owner);
	ClassDB::bind_method(D_METHOD("get_owner"), &Node::get_owner);
	ClassDB::bind_method(D_METHOD("remove_and_skip"), &Node::remove_and_skip);
	ClassDB::bind_method(D_METHOD("get_index"), &Node::get_index);
	ClassDB::bind_method(D_METHOD("print_tree"), &Node::print_tree);
	ClassDB::bind_method(D_METHOD("set_filename", "filename"), &Node::set_filename);
	ClassDB::bind_method(D_METHOD("get_filename"), &Node::get_filename);
	ClassDB::bind_method(D_METHOD("propagate_notification", "what"), &Node::propagate_notification);
	ClassDB::bind_method(D_METHOD("propagate_call", "method", "args", "parent_first"), &Node::propagate_call, DEFVAL(Array()), DEFVAL(false));
	ClassDB::bind_method(D_METHOD("set_fixed_process", "enable"), &Node::set_fixed_process);
	ClassDB::bind_method(D_METHOD("get_fixed_process_delta_time"), &Node::get_fixed_process_delta_time);
	ClassDB::bind_method(D_METHOD("is_fixed_processing"), &Node::is_fixed_processing);
	ClassDB::bind_method(D_METHOD("get_process_delta_time"), &Node::get_process_delta_time);
	ClassDB::bind_method(D_METHOD("set_process", "enable"), &Node::set_process);
	ClassDB::bind_method(D_METHOD("is_processing"), &Node::is_processing);
	ClassDB::bind_method(D_METHOD("set_process_input", "enable"), &Node::set_process_input);
	ClassDB::bind_method(D_METHOD("is_processing_input"), &Node::is_processing_input);
	ClassDB::bind_method(D_METHOD("set_process_unhandled_input", "enable"), &Node::set_process_unhandled_input);
	ClassDB::bind_method(D_METHOD("is_processing_unhandled_input"), &Node::is_processing_unhandled_input);
	ClassDB::bind_method(D_METHOD("set_process_unhandled_key_input", "enable"), &Node::set_process_unhandled_key_input);
	ClassDB::bind_method(D_METHOD("is_processing_unhandled_key_input"), &Node::is_processing_unhandled_key_input);
	ClassDB::bind_method(D_METHOD("set_pause_mode", "mode"), &Node::set_pause_mode);
	ClassDB::bind_method(D_METHOD("get_pause_mode"), &Node::get_pause_mode);
	ClassDB::bind_method(D_METHOD("can_process"), &Node::can_process);
	ClassDB::bind_method(D_METHOD("print_stray_nodes"), &Node::_print_stray_nodes);
	ClassDB::bind_method(D_METHOD("get_position_in_parent"), &Node::get_position_in_parent);
	ClassDB::bind_method(D_METHOD("set_display_folded", "fold"), &Node::set_display_folded);
	ClassDB::bind_method(D_METHOD("is_displayed_folded"), &Node::is_displayed_folded);

	ClassDB::bind_method(D_METHOD("set_process_internal", "enable"), &Node::set_process_internal);
	ClassDB::bind_method(D_METHOD("is_processing_internal"), &Node::is_processing_internal);

	ClassDB::bind_method(D_METHOD("set_fixed_process_internal", "enable"), &Node::set_fixed_process_internal);
	ClassDB::bind_method(D_METHOD("is_fixed_processing_internal"), &Node::is_fixed_processing_internal);

	ClassDB::bind_method(D_METHOD("get_tree"), &Node::get_tree);

	ClassDB::bind_method(D_METHOD("duplicate", "flags"), &Node::duplicate, DEFVAL(DUPLICATE_USE_INSTANCING | DUPLICATE_SIGNALS | DUPLICATE_GROUPS | DUPLICATE_SCRIPTS));
	ClassDB::bind_method(D_METHOD("replace_by", "node", "keep_data"), &Node::replace_by, DEFVAL(false));

	ClassDB::bind_method(D_METHOD("get_viewport"), &Node::get_viewport);

	ClassDB::bind_method(D_METHOD("queue_free"), &Node::queue_delete);

	ClassDB::bind_method(D_METHOD("request_ready"), &Node::request_ready);

	BIND_CONSTANT(NOTIFICATION_ENTER_TREE);
	BIND_CONSTANT(NOTIFICATION_EXIT_TREE);
	BIND_CONSTANT(NOTIFICATION_MOVED_IN_PARENT);
	BIND_CONSTANT(NOTIFICATION_READY);
	BIND_CONSTANT(NOTIFICATION_PAUSED);
	BIND_CONSTANT(NOTIFICATION_UNPAUSED);
	BIND_CONSTANT(NOTIFICATION_FIXED_PROCESS);
	BIND_CONSTANT(NOTIFICATION_PROCESS);
	BIND_CONSTANT(NOTIFICATION_PARENTED);
	BIND_CONSTANT(NOTIFICATION_UNPARENTED);
	BIND_CONSTANT(NOTIFICATION_DRAG_BEGIN);
	BIND_CONSTANT(NOTIFICATION_DRAG_END);
	BIND_CONSTANT(NOTIFICATION_PATH_CHANGED);
	BIND_CONSTANT(NOTIFICATION_TRANSLATION_CHANGED);
	BIND_CONSTANT(NOTIFICATION_INTERNAL_PROCESS);
	BIND_CONSTANT(NOTIFICATION_INTERNAL_FIXED_PROCESS);

	BIND_ENUM_CONSTANT(PAUSE_MODE_INHERIT);
	BIND_ENUM_CONSTANT(PAUSE_MODE_STOP);
	BIND_ENUM_CONSTANT(PAUSE_MODE_PROCESS);

	BIND_ENUM_CONSTANT(DUPLICATE_SIGNALS);
	BIND_ENUM_CONSTANT(DUPLICATE_GROUPS);
	BIND_ENUM_CONSTANT(DUPLICATE_SCRIPTS);
	BIND_ENUM_CONSTANT(DUPLICATE_USE_INSTANCING);

	ADD_SIGNAL(MethodInfo("renamed"));
	ADD_SIGNAL(MethodInfo("tree_entered"));
	ADD_SIGNAL(MethodInfo("tree_exiting"));
	ADD_SIGNAL(MethodInfo("tree_exited"));

	//ADD_PROPERTYNZ( PropertyInfo( Variant::BOOL, "process/process" ),"set_process","is_processing") ;
	//ADD_PROPERTYNZ( PropertyInfo( Variant::BOOL, "process/fixed_process" ), "set_fixed_process","is_fixed_processing") ;
	//ADD_PROPERTYNZ( PropertyInfo( Variant::BOOL, "process/input" ), "set_process_input","is_processing_input" ) ;
	//ADD_PROPERTYNZ( PropertyInfo( Variant::BOOL, "process/unhandled_input" ), "set_process_unhandled_input","is_processing_unhandled_input" ) ;
	ADD_GROUP("Pause", "pause_");
	ADD_PROPERTYNZ(PropertyInfo(Variant::INT, "pause_mode", PROPERTY_HINT_ENUM, "Inherit,Stop,Process"), "set_pause_mode", "get_pause_mode");
	ADD_PROPERTYNZ(PropertyInfo(Variant::BOOL, "editor/display_folded", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NOEDITOR | PROPERTY_USAGE_INTERNAL), "set_display_folded", "is_displayed_folded");
	ADD_PROPERTYNZ(PropertyInfo(Variant::STRING, "name", PROPERTY_HINT_NONE, "", 0), "set_name", "get_name");
	ADD_PROPERTYNZ(PropertyInfo(Variant::STRING, "filename", PROPERTY_HINT_NONE, "", 0), "set_filename", "get_filename");
	ADD_PROPERTYNZ(PropertyInfo(Variant::OBJECT, "owner", PROPERTY_HINT_RESOURCE_TYPE, "Node", 0), "set_owner", "get_owner");

	BIND_VMETHOD(MethodInfo("_process", PropertyInfo(Variant::REAL, "delta")));
	BIND_VMETHOD(MethodInfo("_fixed_process", PropertyInfo(Variant::REAL, "delta")));
	BIND_VMETHOD(MethodInfo("_enter_tree"));
	BIND_VMETHOD(MethodInfo("_exit_tree"));
	BIND_VMETHOD(MethodInfo("_ready"));
	BIND_VMETHOD(MethodInfo("_input", PropertyInfo(Variant::OBJECT, "event", PROPERTY_HINT_RESOURCE_TYPE, "InputEvent")));
	BIND_VMETHOD(MethodInfo("_unhandled_input", PropertyInfo(Variant::OBJECT, "event", PROPERTY_HINT_RESOURCE_TYPE, "InputEvent")));
	BIND_VMETHOD(MethodInfo("_unhandled_key_input", PropertyInfo(Variant::OBJECT, "event", PROPERTY_HINT_RESOURCE_TYPE, "InputEventKey")));

	//ClassDB::bind_method(D_METHOD("get_child",&Node::get_child,PH("index")));
	//ClassDB::bind_method(D_METHOD("get_node",&Node::get_node,PH("path")));
}

String Node::_get_name_num_separator() {
	switch (ProjectSettings::get_singleton()->get("node/name_num_separator").operator int()) {
		case 0: return "";
		case 1: return " ";
		case 2: return "_";
		case 3: return "-";
	}
	return " ";
}

Node::Node() {

	data.pos = -1;
	data.depth = -1;
	data.blocked = 0;
	data.parent = NULL;
	data.tree = NULL;
	data.fixed_process = false;
	data.idle_process = false;
	data.fixed_process_internal = false;
	data.idle_process_internal = false;
	data.inside_tree = false;
	data.ready_notified = false;

	data.owner = NULL;
	data.OW = NULL;
	data.input = false;
	data.unhandled_input = false;
	data.unhandled_key_input = false;
	data.pause_mode = PAUSE_MODE_INHERIT;
	data.pause_owner = NULL;
	data.path_cache = NULL;
	data.parent_owned = false;
	data.in_constructor = true;
	data.viewport = NULL;
	data.display_folded = false;
	data.ready_first = true;
}

Node::~Node() {

	data.grouped.clear();
	data.owned.clear();
	data.children.clear();

	ERR_FAIL_COND(data.parent);
	ERR_FAIL_COND(data.children.size());
}

////////////////////////////////
