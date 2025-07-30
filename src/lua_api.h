#ifndef LUA_API_H
#define LUA_API_H

#include <lua.hpp> // Include Lua's C++ headers

// Forward declaration of Editor class to avoid circular includes
// The actual Editor class definition is in editor.h
class Editor;

// Declare all C functions that will be exposed to Lua
// These functions take a lua_State* as their argument and return an int (number of return values)

// Editor state and information
int lua_set_status_message(lua_State* L);
int lua_get_current_time_ms(lua_State* L);
int lua_refresh_screen(lua_State* L);
int lua_force_full_redraw(lua_State* L);

// Text buffer manipulation
int lua_insert_text(lua_State* L);
int lua_get_current_line_text(lua_State* L);
int lua_get_line_count(lua_State* L);
// Add the new functions we discussed:
int lua_get_line(lua_State* L);
int lua_set_line(lua_State* L);
int lua_insert_line(lua_State* L);
int lua_delete_line(lua_State* L);
int lua_get_buffer_content(lua_State* L);
int lua_set_buffer_content(lua_State* L);
int lua_get_char_at(lua_State* L);
int lua_get_tab_stop_width(lua_State* L);


// Cursor information
int lua_get_cursor_x(lua_State* L);
int lua_get_cursor_y(lua_State* L);
int lua_set_cursor_position(lua_State* L);
int lua_set_row_offset(lua_State* L);
int lua_set_col_offset(lua_State* L);
int lua_get_row_offset(lua_State* L);
int lua_get_col_offset(lua_State* L);
int lua_center_view_on_cursor(lua_State* L);

// File system and editor actions
int lua_get_current_filename(lua_State* L);
int lua_open_file(lua_State* L);
int lua_save_file(lua_State* L);
int lua_is_dirty(lua_State* L);
int lua_get_directory_path(lua_State* L);
int lua_set_directory_path(lua_State* L);
int lua_list_directory(lua_State* L);
int lua_create_file(lua_State* L);
int lua_create_directory(lua_State* L);
int lua_delete_file(lua_State* L);
int lua_delete_directory(lua_State* L);

// User Interaction
int lua_prompt_user(lua_State* L);
int lua_show_message(lua_State* L);
int lua_show_error(lua_State* L);
int lua_get_screen_size(lua_State* L);

// Terminal Interaction
int lua_send_terminal_input(lua_State* L);
int lua_toggle_terminal(lua_State* L);

// Helper Functions
int lua_path_join(lua_State* L);
int lua_file_exists(lua_State* L);
int lua_is_directory(lua_State* L);
int lua_execute_command(lua_State* L);

// Event registration
int lua_register_event_handler(lua_State* L);
int lua_is_ctrl_pressed(lua_State* L); // For checking key modifiers

// Plugin data persistence
int lua_save_plugin_data(lua_State* L);
int lua_load_plugin_data(lua_State* L);


// Array to register all functions in Lua
// This should be extern if defined in a .cpp file and used here,
// or defined directly in the .cpp file where it's used.
// For now, keep it in Editor.cpp near exposeEditorToLua()
// static const luaL_Reg editor_lib[]; // This declaration would go here if defined externally
                                     // But it's better to keep it in Editor.cpp for implementation details.

#endif // LUA_API_H