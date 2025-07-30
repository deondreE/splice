#include "editor.h"
#include <sstream>
#include <algorithm>
#include "win_console_utils.h"
#include <iostream>
#include "lua_api.h"
#include <Shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")

DWORD WINAPI TerminalOutputReaderThread(LPVOID lpParam);

#pragma region  Lua

int lua_get_current_filename(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    lua_pushstring(L, editor->filename.c_str());
    return 1;
}

int lua_set_status_message(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");

    // Check arguments: expects (string message)
    if (!lua_isstring(L, 1)) {
        return luaL_error(L, "Argument #1 (message) for set_status_message must be a string.");
    }
    std::string message = lua_tostring(L, 1);

    // Optional: a duration can be passed as a second argument (as discussed previously)
    ULONGLONG duration_ms = 5000; // Default 5 seconds, match original logic
    if (lua_isinteger(L, 2)) {
        duration_ms = lua_tointeger(L, 2);
    }

    editor->statusMessage = message;
    // Set expiry time based on current tick count + duration
    editor->statusMessageTime = GetTickCount64() + duration_ms;
    return 0; // Number of return values on the Lua stack
}

int lua_get_current_time_ms(lua_State* L) {
    // GetTickCount64() is a Windows-specific function.
    // If you plan cross-platform, you'd use a C++ standard library chrono function.
    lua_pushinteger(L, GetTickCount64());
    return 1;
}

int lua_refresh_screen(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");

    // Call the main screen refresh logic
    editor->refreshScreen();
    return 0;
}

int lua_insert_text(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");

    // Check arguments: expects (string text)
    if (!lua_isstring(L, 1)) {
        return luaL_error(L, "Argument #1 (text) for insert_text must be a string.");
    }
    std::string text = lua_tostring(L, 1);

    // Call the Editor's internal method to insert characters
    // This loops through chars, handling newlines and tab expansion correctly.
    for (char c : text) {
        if (c == '\n') {
            editor->insertNewline();
        } else {
            editor->insertChar(c);
        }
    }
    editor->dirty = true; // Mark buffer as modified
    editor->calculateLineNumberWidth(); // Recalculate if lines added/removed
    editor->triggerEvent("on_buffer_changed"); // Notify plugins of buffer change
    editor->triggerEvent("on_cursor_moved");   // Notify plugins of cursor movement
    return 0;
}

int lua_get_current_line_text(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");

    std::string line_text = "";
    // Ensure cursorY is within valid bounds before accessing `lines` vector
    if (editor->cursorY >= 0 && editor->cursorY < editor->lines.size()) {
        line_text = editor->lines[editor->cursorY];
    }
    lua_pushstring(L, line_text.c_str());
    return 1; // Return 1 value (the string)
}

int lua_get_line_count(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    lua_pushinteger(L, editor->lines.size());
    return 1;
}

int lua_get_cursor_y(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    lua_pushinteger(L, editor->cursorY + 1); // Lua is 1-based for display
    return 1;
}

int lua_force_full_redraw(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    editor->force_full_redraw_internal(); // Call the Editor's member function
    return 0;
}

int lua_get_cursor_x(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    lua_pushinteger(L, editor->cursorX + 1); // Lua is 1-based for display
    return 1;
}

int lua_get_line(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    if (!lua_isinteger(L, 1)) return luaL_error(L, "Argument #1 (line_number) must be an integer.");

    int line_num = lua_tointeger(L, 1) - 1; // Lua is 1-based, C++ is 0-based
    if (line_num >= 0 && line_num < editor->lines.size()) {
        lua_pushstring(L, editor->lines[line_num].c_str());
    } else {
        lua_pushstring(L, ""); // Return empty string for out-of-bounds lines
    }
    return 1;
}

int lua_set_line(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    if (!lua_isinteger(L, 1)) return luaL_error(L, "Argument #1 (line_number) must be an integer.");
    if (!lua_isstring(L, 2)) return luaL_error(L, "Argument #2 (text) must be a string.");

    int line_num = lua_tointeger(L, 1) - 1;
    std::string new_text = lua_tostring(L, 2);

    if (line_num >= 0 && line_num < editor->lines.size()) {
        editor->lines[line_num] = new_text;
        editor->dirty = true;
        editor->calculateLineNumberWidth();
        editor->force_full_redraw_internal(); // Use the internal method
        editor->triggerEvent("on_buffer_changed"); // Trigger buffer changed event
    } else {
        return luaL_error(L, "Line number %d is out of bounds.", line_num + 1);
    }
    return 0;
}

int lua_insert_line(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    if (!lua_isinteger(L, 1)) return luaL_error(L, "Argument #1 (line_number) must be an integer.");
    if (!lua_isstring(L, 2)) return luaL_error(L, "Argument #2 (text) must be a string.");

    int line_num = lua_tointeger(L, 1) - 1;
    std::string text = lua_tostring(L, 2);

    if (line_num >= 0 && line_num <= editor->lines.size()) { // Can insert at end (size())
        editor->lines.insert(editor->lines.begin() + line_num, text);
        editor->dirty = true;
        editor->calculateLineNumberWidth();
        editor->force_full_redraw_internal();
        editor->triggerEvent("on_buffer_changed");
    } else {
        return luaL_error(L, "Insertion line number %d is out of bounds.", line_num + 1);
    }
    return 0;
}

int lua_delete_line(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    if (!lua_isinteger(L, 1)) return luaL_error(L, "Argument #1 (line_number) must be an integer.");

    int line_num = lua_tointeger(L, 1) - 1;

    if (editor->lines.empty()) return 0; // Nothing to delete
    if (line_num >= 0 && line_num < editor->lines.size()) {
        editor->lines.erase(editor->lines.begin() + line_num);
        if (editor->lines.empty()) { // Ensure at least one empty line
            editor->lines.push_back("");
        }
        editor->dirty = true;
        editor->calculateLineNumberWidth();
        editor->force_full_redraw_internal();
        editor->triggerEvent("on_buffer_changed");
    } else {
        return luaL_error(L, "Deletion line number %d is out of bounds.", line_num + 1);
    }
    return 0;
}

int lua_get_buffer_content(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");

    lua_newtable(L); // Create a new Lua table
    for (int i = 0; i < editor->lines.size(); ++i) {
        lua_pushstring(L, editor->lines[i].c_str());
        lua_rawseti(L, -2, i + 1); // Set table[i+1] = line_content (Lua is 1-based)
    }
    return 1; // Return the table
}

int lua_set_buffer_content(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    if (!lua_istable(L, 1)) return luaL_error(L, "Argument #1 (content) must be a table of strings.");

    editor->lines.clear();
    int table_len = luaL_len(L, 1); // Get table length
    for (int i = 1; i <= table_len; ++i) {
        lua_rawgeti(L, 1, i); // Get table[i]
        if (lua_isstring(L, -1)) {
            editor->lines.push_back(lua_tostring(L, -1));
        } else {
            // Handle non-string elements if necessary, e.g., push an empty string
            editor->lines.push_back("");
        }
        lua_pop(L, 1); // Pop the value
    }
    if (editor->lines.empty()) editor->lines.push_back(""); // Ensure at least one line

    editor->cursorX = 0;
    editor->cursorY = 0;
    editor->rowOffset = 0;
    editor->colOffset = 0;
    editor->dirty = true;
    editor->calculateLineNumberWidth();
    editor->force_full_redraw_internal();
    editor->triggerEvent("on_buffer_changed");
    return 0;
}

int lua_get_char_at(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    if (!lua_isinteger(L, 1) || !lua_isinteger(L, 2)) return luaL_error(L, "Arguments (line, col) must be integers.");

    int line = lua_tointeger(L, 1) - 1; // Lua 1-based to C++ 0-based
    int col = lua_tointeger(L, 2) - 1;

    if (line >= 0 && line < editor->lines.size() && col >= 0 && col < editor->lines[line].length()) {
        lua_pushstring(L, std::string(1, editor->lines[line][col]).c_str());
    } else {
        lua_pushnil(L); // Return nil if out of bounds
    }
    return 1;
}

int lua_get_tab_stop_width(lua_State* L) {
    lua_pushinteger(L, KILO_TAB_STOP); // KILO_TAB_STOP must be accessible (e.g., in editor.h)
    return 1;
}

int lua_set_cursor_position(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    if (!lua_isinteger(L, 1) || !lua_isinteger(L, 2)) return luaL_error(L, "Arguments (x, y) must be integers.");

    int x = lua_tointeger(L, 1) - 1;
    int y = lua_tointeger(L, 2) - 1;

    // Clamp cursor to valid positions
    if (y < 0) y = 0;
    if (y >= editor->lines.size()) y = editor->lines.size() - 1;
    if (editor->lines.empty()) { // Handle empty file special case
        y = 0; x = 0;
    } else {
        if (x < 0) x = 0;
        if (x > editor->lines[y].length()) x = editor->lines[y].length();
    }

    editor->cursorX = x;
    editor->cursorY = y;
    editor->scroll(); // Adjust scroll to make cursor visible
    return 0;
}

int lua_set_row_offset(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    if (!lua_isinteger(L, 1)) return luaL_error(L, "Argument #1 (offset) must be an integer.");
    editor->rowOffset = std::max(0, (int)lua_tointeger(L, 1));
    editor->force_full_redraw_internal();
    return 0;
}

int lua_set_col_offset(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    if (!lua_isinteger(L, 1)) return luaL_error(L, "Argument #1 (offset) must be an integer.");
    editor->colOffset = std::max(0, (int)lua_tointeger(L, 1));
    editor->force_full_redraw_internal();
    return 0;
}

int lua_get_row_offset(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    lua_pushinteger(L, editor->rowOffset);
    return 1;
}

int lua_get_col_offset(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    lua_pushinteger(L, editor->colOffset);
    return 1;
}

int lua_center_view_on_cursor(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    editor->scroll(); // `scroll` already tries to keep the cursor in view.
                      // To truly center, you'd calculate target offsets based on cursor.
                      // For now, `scroll` is sufficient.
    return 0;
}


int lua_open_file(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    if (!lua_isstring(L, 1)) return luaL_error(L, "Argument #1 (path) must be a string.");

    std::string path = lua_tostring(L, 1);
    bool success = editor->openFile(path);
    lua_pushboolean(L, success);
    return 1;
}

int lua_save_file(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    bool success = editor->saveFile();
    lua_pushboolean(L, success);
    return 1;
}

int lua_is_dirty(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    lua_pushboolean(L, editor->dirty);
    return 1;
}

int lua_get_directory_path(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    lua_pushstring(L, editor->currentDirPath.c_str());
    return 1;
}

int lua_set_directory_path(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    if (!lua_isstring(L, 1)) return luaL_error(L, "Argument #1 (path) must be a string.");
    std::string path = lua_tostring(L, 1);
    editor->populateDirectoryEntries(path); // Re-use existing populate logic
    // Also update internal currentDirPath member if it's not done in populateDirectoryEntries
    editor->currentDirPath = path;
    return 0;
}

// Implementing lua_list_directory would involve replicating some of populateDirectoryEntries
// but returning the result as a Lua table of tables { {name="file", is_dir=true}, ... }
int lua_list_directory(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    std::string path = lua_tostring(L, 1); // Optional: path, else use currentDirPath

    std::string targetPath = path.empty() ? editor->currentDirPath : path;

    std::vector<DirEntry> entries;
    WIN32_FIND_DATAA findFileData;
    HANDLE hFind = FindFirstFileA((targetPath + "\\*").c_str(), &findFileData);

    if (hFind == INVALID_HANDLE_VALUE) {
        lua_pushnil(L); // Indicate failure
        lua_pushstring(L, ("Error: Could not list directory '" + targetPath + "'").c_str());
        return 2;
    }

    do {
        std::string entryName = findFileData.cFileName;
        if (entryName == "." || entryName == "..") {
            continue;
        }
        bool isDir = (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        entries.push_back({ entryName, isDir });
    } while (FindNextFileA(hFind, &findFileData) != 0);
    FindClose(hFind);

    // Sort entries (optional, but good practice)
    std::sort(entries.begin(), entries.end(), [](const DirEntry& a, const DirEntry& b) {
        if (a.isDirectory && !b.isDirectory) return true;
        if (!a.isDirectory && b.isDirectory) return false;
        return a.name < b.name;
    });

    lua_newtable(L); // Main table to hold directory entries
    for (int i = 0; i < entries.size(); ++i) {
        lua_newtable(L); // Table for each entry
        lua_pushstring(L, entries[i].name.c_str());
        lua_setfield(L, -2, "name");
        lua_pushboolean(L, entries[i].isDirectory);
        lua_setfield(L, -2, "is_directory");
        lua_rawseti(L, -2, i + 1); // Add entry table to main table
    }
    return 1; // Return the table of entries
}

int lua_create_file(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    if (!lua_isstring(L, 1)) return luaL_error(L, "Argument #1 (path) must be a string.");
    std::string path = lua_tostring(L, 1);

    HANDLE hFile = CreateFileA(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        lua_pushboolean(L, false);
        lua_pushstring(L, ("Failed to create file: " + std::to_string(GetLastError())).c_str());
        return 2;
    }
    CloseHandle(hFile);
    lua_pushboolean(L, true);
    return 1;
}

int lua_create_directory(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    if (!lua_isstring(L, 1)) return luaL_error(L, "Argument #1 (path) must be a string.");
    std::string path = lua_tostring(L, 1);

    if (CreateDirectoryA(path.c_str(), NULL)) {
        lua_pushboolean(L, true);
        return 1;
    } else {
        lua_pushboolean(L, false);
        lua_pushstring(L, ("Failed to create directory: " + std::to_string(GetLastError())).c_str());
        return 2;
    }
}

int lua_delete_file(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    if (!lua_isstring(L, 1)) return luaL_error(L, "Argument #1 (path) must be a string.");
    std::string path = lua_tostring(L, 1);

    if (DeleteFileA(path.c_str())) {
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
        lua_pushstring(L, ("Failed to delete file: " + std::to_string(GetLastError())).c_str());
        return 2;
    }
    return 1;
}

int lua_delete_directory(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    if (!lua_isstring(L, 1)) return luaL_error(L, "Argument #1 (path) must be a string.");
    std::string path = lua_tostring(L, 1);

    if (RemoveDirectoryA(path.c_str())) {
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
        lua_pushstring(L, ("Failed to remove directory: " + std::to_string(GetLastError())).c_str());
        return 2;
    }
    return 1;
}

// You'll need to adapt Editor::promptUser to be driven by lua_prompt_user
// and potentially resume the Lua coroutine/call a Lua callback when input is ready.
// For now, a simplified non-blocking version:
int lua_prompt_user(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    if (!lua_isstring(L, 1)) return luaL_error(L, "Argument #1 (prompt_message) must be a string.");

    std::string prompt_msg = lua_tostring(L, 1);
    std::string default_val = "";
    if (lua_isstring(L, 2)) {
        default_val = lua_tostring(L, 2);
    }

    // Store the Lua state and potentially a callback for later resumption
    // This simplified version just sets the mode and assumes C++ event loop will handle
    editor->mode = PROMPT_MODE;
    editor->promptMessage = prompt_msg;
    editor->searchQuery = default_val; // Reusing searchQuery for user input
    editor->statusMessage = editor->promptMessage + editor->searchQuery;
    editor->statusMessageTime = GetTickCount64();
    editor->force_full_redraw_internal();

    // This would typically return a temporary value or nil and then rely on a callback
    // or a C++ side mechanism to push the result to Lua when input is finished.
    // For a true blocking call (from Lua's perspective), you'd need Lua coroutines.
    // For now, let's just indicate that the prompt is active.
    lua_pushboolean(L, true); // Indicate prompt started
    return 1;
}

// A helper for showing messages with explicit duration
int lua_show_message(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    if (!lua_isstring(L, 1)) return luaL_error(L, "Argument #1 (message) must be a string.");

    std::string message = lua_tostring(L, 1);
    ULONGLONG duration_ms = 5000; // Default 5 seconds
    if (lua_isinteger(L, 2)) {
        duration_ms = lua_tointeger(L, 2);
    }

    editor->statusMessage = message;
    editor->statusMessageTime = GetTickCount64() + duration_ms; // Set expiry time
    return 0;
}

int lua_show_error(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    if (!lua_isstring(L, 1)) return luaL_error(L, "Argument #1 (error_message) must be a string.");

    std::string message = "Error: " + std::string(lua_tostring(L, 1));
    ULONGLONG duration_ms = 8000; // Default 8 seconds for errors
    if (lua_isinteger(L, 2)) {
        duration_ms = lua_tointeger(L, 2);
    }

    editor->statusMessage = message;
    editor->statusMessageTime = GetTickCount64() + duration_ms;
    // Optionally set a different color attribute for error messages in Editor::drawMessageBar()
    return 0;
}

int lua_get_screen_size(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    lua_pushinteger(L, editor->screenCols);
    lua_pushinteger(L, editor->screenRows);
    return 2;
}

int lua_send_terminal_input(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    if (!lua_isstring(L, 1)) return luaL_error(L, "Argument #1 (input_text) must be a string.");

    std::string input = lua_tostring(L, 1);
    editor->writeTerminalInput(input);
    return 0;
}

int lua_toggle_terminal(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    editor->toggleTerminal();
    return 0;
}

int lua_path_join(lua_State* L) {
    // Lua function will take variable number of string arguments
    int n_args = lua_gettop(L);
    if (n_args == 0) {
        lua_pushstring(L, "");
        return 1;
    }

    std::string result_path = lua_tostring(L, 1);
    for (int i = 2; i <= n_args; ++i) {
        if (!lua_isstring(L, i)) {
            return luaL_error(L, "Argument #%d must be a string.", i);
        }
        std::string part = lua_tostring(L, i);
        char combined[MAX_PATH];
        if (!PathCombineA(combined, result_path.c_str(), part.c_str())) {
            return luaL_error(L, "Failed to combine paths: %s + %s", result_path.c_str(), part.c_str());
        }
        result_path = combined;
    }
    lua_pushstring(L, result_path.c_str());
    return 1;
}

int lua_file_exists(lua_State* L) {
    if (!lua_isstring(L, 1)) return luaL_error(L, "Argument #1 (path) must be a string.");
    std::string path = lua_tostring(L, 1);
    DWORD attributes = GetFileAttributesA(path.c_str());
    lua_pushboolean(L, (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY)));
    return 1;
}

int lua_is_directory(lua_State* L) {
    if (!lua_isstring(L, 1)) return luaL_error(L, "Argument #1 (path) must be a string.");
    std::string path = lua_tostring(L, 1);
    DWORD attributes = GetFileAttributesA(path.c_str());
    lua_pushboolean(L, (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY)));
    return 1;
}

int lua_execute_command(lua_State* L) {
    if (!lua_isstring(L, 1)) return luaL_error(L, "Argument #1 (command) must be a string.");
    std::string command = lua_tostring(L, 1);

    // This is a very basic execution and won't capture output or handle interactivity.
    // For proper handling, you'd integrate with your terminal logic or CreateProcessWithToken/ShellExecuteEx
    int result = system(command.c_str()); // system() blocks and outputs to console
    lua_pushinteger(L, result);
    return 1;
}

int lua_register_event_handler(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    if (!lua_isstring(L, 1)) return luaL_error(L, "Argument #1 (event_name) must be a string.");
    if (!lua_isfunction(L, 2)) return luaL_error(L, "Argument #2 (handler_function) must be a function.");

    std::string event_name = lua_tostring(L, 1);
    lua_pushvalue(L, 2); // Push the function onto the top of the stack
    int funcRef = luaL_ref(L, LUA_REGISTRYINDEX); // Store a reference to it

    // Store with its L_state for correct context when triggering
    if (event_name == "on_key_press") {
        editor->onKeyPressCallbacks.push_back({funcRef, L});
    } else if (event_name == "on_file_opened") {
        editor->onFileOpenedCallbacks.push_back({funcRef, L});
    } else if (event_name == "on_file_saved") {
        editor->onFileSavedCallbacks.push_back({funcRef, L});
    } else if (event_name == "on_buffer_changed") {
        editor->onBufferChangedCallbacks.push_back({funcRef, L});
    } else if (event_name == "on_cursor_moved") {
        editor->onCursorMovedCallbacks.push_back({funcRef, L});
    } else if (event_name == "on_mode_changed") {
        editor->onModeChangedCallbacks.push_back({funcRef, L});
    } else {
        luaL_unref(L, LUA_REGISTRYINDEX, funcRef); // Unref if unknown
        return luaL_error(L, "Unknown event name: %s", event_name.c_str());
    }
    return 0;
}

int lua_is_ctrl_pressed(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    // This requires Editor to track Ctrl key state from input polling
    lua_pushboolean(L, editor->ctrl_pressed);
    return 1;
}

// --- Plugin Data Persistence ---
// These would typically save/load JSON/INI to a plugin-specific file
// For simplicity, let's use a dummy implementation for now.
// A proper implementation would need a mechanism to map 'key' to a filename,
// and handle serialization/deserialization (e.g., using a Lua JSON library or custom C++ code).

// Dummy storage for plugin data
static std::map<std::string, std::string> plugin_data_storage; // Use map for simplicity, convert to file IO later

int lua_save_plugin_data(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    if (!lua_isstring(L, 1)) return luaL_error(L, "Argument #1 (key) must be a string.");
    if (!lua_isstring(L, 2)) return luaL_error(L, "Argument #2 (value) must be a string.");

    std::string key = lua_tostring(L, 1);
    std::string value = lua_tostring(L, 2);

    // In a real scenario, serialize value (e.g., a table) to JSON/string and write to file.
    // For this dummy, we just store the string.
    plugin_data_storage[key] = value;
    editor->statusMessage = "Plugin data saved for key: " + key;
    editor->statusMessageTime = GetTickCount64();
    lua_pushboolean(L, true);
    return 1;
}

int lua_load_plugin_data(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    if (!lua_isstring(L, 1)) return luaL_error(L, "Argument #1 (key) must be a string.");

    std::string key = lua_tostring(L, 1);
    if (plugin_data_storage.count(key)) {
        // In a real scenario, read from file and deserialize into Lua table/value.
        lua_pushstring(L, plugin_data_storage[key].c_str());
        return 1;
    } else {
        lua_pushnil(L); // Not found
        return 1;
    }
}

// Define the global static map outside any function
std::map<std::string, std::string> Editor::plugin_data_storage;


// Update the `editor_lib` array to include all new functions
static const luaL_Reg editor_lib[] = {
    // Existing functions that are now fully implemented or refined
    {"set_status_message", lua_set_status_message}, // Refined to accept duration
    {"get_current_time_ms", lua_get_current_time_ms},
    {"refresh_screen", lua_refresh_screen},
    {"force_full_redraw", lua_force_full_redraw}, // Now calls Editor::force_full_redraw_internal()
    {"insert_text", lua_insert_text},             // Refined to use insertChar/insertNewline
    {"get_current_line_text", lua_get_current_line_text},
    {"get_line_count", lua_get_line_count},
    {"get_cursor_x", lua_get_cursor_x},             // Returns 1-based index
    {"get_cursor_y", lua_get_cursor_y},             // Returns 1-based index
    {"get_current_filename", lua_get_current_filename},

    // New functions from the expanded API
    {"get_line", lua_get_line},
    {"set_line", lua_set_line},
    {"insert_line", lua_insert_line},
    {"delete_line", lua_delete_line},
    {"get_buffer_content", lua_get_buffer_content},
    {"set_buffer_content", lua_set_buffer_content},
    {"get_char_at", lua_get_char_at},
    {"get_tab_stop_width", lua_get_tab_stop_width},
    {"set_cursor_position", lua_set_cursor_position}, // Expects 1-based x, y
    {"set_row_offset", lua_set_row_offset},
    {"set_col_offset", lua_set_col_offset},
    {"get_row_offset", lua_get_row_offset},
    {"get_col_offset", lua_get_col_offset},
    {"center_view_on_cursor", lua_center_view_on_cursor},
    {"open_file", lua_open_file},
    {"save_file", lua_save_file},
    {"is_dirty", lua_is_dirty},
    {"get_directory_path", lua_get_directory_path},
    {"set_directory_path", lua_set_directory_path},
    {"list_directory", lua_list_directory},
    {"create_file", lua_create_file},
    {"create_directory", lua_create_directory},
    {"delete_file", lua_delete_file},
    {"delete_directory", lua_delete_directory},
    {"prompt", lua_prompt_user}, // 'prompt' is a nicer Lua name, initiates prompt mode
    {"show_message", lua_show_message},
    {"show_error", lua_show_error},
    {"get_screen_size", lua_get_screen_size},
    {"send_terminal_input", lua_send_terminal_input},
    {"toggle_terminal", lua_toggle_terminal},
    {"path_join", lua_path_join},
    {"file_exists", lua_file_exists},
    {"is_directory", lua_is_directory},
    {"execute_command", lua_execute_command},
    {"register_event", lua_register_event_handler},
    {"is_ctrl_pressed", lua_is_ctrl_pressed},
    {"save_plugin_data", lua_save_plugin_data},
    {"load_plugin_data", lua_load_plugin_data},
    {NULL, NULL}  // Sentinel to mark the end of the array
};
#pragma endregion

Editor::Editor() : 
    cursorX(0), 
    cursorY(0), 
    screenRows(0), 
    screenCols(0),
    rowOffset(0), 
    colOffset(0), 
    lineNumberWidth(0),
    statusMessage("HELP: Ctrl-Q = quit | Ctrl-S = save | Ctrl-O = open | Ctrl-E = explorer | Ctrl-F = find | Ctrl-L = plugins"),
    statusMessageTime(GetTickCount64()),
    prevStatusMessage(""), 
    prevMessageBarMessage(""), 
    dirty(false),
    mode(EDIT_MODE), 
    selectedFileIndex(0),
    fileExplorerScrollOffset(0),
    currentMatchIndex(-1), 
    originalCursorX(0), 
    originalCursorY(0),
    originalRowOffset(0), 
    originalColOffset(0), 
    L(nullptr),
    terminalActive(false),
    terminalCursorX(0),
    terminalScrollOffset(0),
    terminalHeight(0),
    terminalWidth(0),
    hChildStdinRead(NULL), hChildStdinWrite(NULL), hChildStdoutRead(NULL), hChildStdoutWrite(NULL),
    currentFgColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE), // Default white
    currentBgColor(0),
    currentBold(false),
    defaultFgColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE),
    defaultBgColor(0)
{
    lines.push_back("");
    updateScreenSize();
    prevDrawnLines.resize(screenRows - 2);
    for (auto& s : prevDrawnLines) s.assign(screenCols, ' ');

    char buffer[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, buffer);
    currentDirPath = buffer;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        defaultFgColor = csbi.wAttributes & 0x0F;
        defaultBgColor = csbi.wAttributes & 0x0F;
        currentFgColor = defaultFgColor;
        currentBgColor = defaultBgColor;
    }

    clearTerminalBuffer();

    initializeLua();
    loadLuaPlugins(); 
}

Editor::~Editor() {
    finalizeLua();
    for (const auto& cb : onKeyPressCallbacks) luaL_unref(cb.L_state, LUA_REGISTRYINDEX, cb.funcRef);
    for (const auto& cb : onFileOpenedCallbacks) luaL_unref(cb.L_state, LUA_REGISTRYINDEX, cb.funcRef);
    for (const auto& cb : onFileSavedCallbacks) luaL_unref(cb.L_state, LUA_REGISTRYINDEX, cb.funcRef);
    for (const auto& cb : onBufferChangedCallbacks) luaL_unref(cb.L_state, LUA_REGISTRYINDEX, cb.funcRef);
    for (const auto& cb : onCursorMovedCallbacks) luaL_unref(cb.L_state, LUA_REGISTRYINDEX, cb.funcRef);
    for (const auto& cb : onModeChangedCallbacks) luaL_unref(cb.L_state, LUA_REGISTRYINDEX, cb.funcRef);
    stopTerminal();
}

void Editor::triggerEvent(const std::string& eventName, int param) {
    std::vector<LuaCallback>* callbacks = nullptr;
    if (eventName == "on_key_press") callbacks = &onKeyPressCallbacks;
    else if (eventName == "on_mode_changed") callbacks = &onModeChangedCallbacks;
    // ... add more if they take int params

    if (callbacks) {
        for (const auto& callback : *callbacks) {
            lua_rawgeti(callback.L_state, LUA_REGISTRYINDEX, callback.funcRef);
            lua_pushinteger(callback.L_state, param);
            int status = lua_pcall(callback.L_state, 1, 0, 0); // 1 arg, 0 results
            if (status != LUA_OK) {
                std::cerr << "Error in Lua event '" << eventName << "': " << lua_tostring(callback.L_state, -1) << std::endl;
                lua_pop(callback.L_state, 1);
            }
        }
    }
}

void Editor::triggerEvent(const std::string& eventName, const std::string& param) {
    std::vector<LuaCallback>* callbacks = nullptr;
    if (eventName == "on_file_opened") callbacks = &onFileOpenedCallbacks;
    else if (eventName == "on_file_saved") callbacks = &onFileSavedCallbacks;
    // ... add more if they take string params

    if (callbacks) {
        for (const auto& callback : *callbacks) {
            lua_rawgeti(callback.L_state, LUA_REGISTRYINDEX, callback.funcRef);
            lua_pushstring(callback.L_state, param.c_str());
            int status = lua_pcall(callback.L_state, 1, 0, 0); // 1 arg, 0 results
            if (status != LUA_OK) {
                std::cerr << "Error in Lua event '" << eventName << "': " << lua_tostring(callback.L_state, -1) << std::endl;
                lua_pop(callback.L_state, 1);
            }
        }
    }
}

void Editor::initializeLua() {
    L = luaL_newstate(); // Create new Lua state
    if (!L) {
        std::cerr << "Error: Failed to create Lua state." << std::endl;
        exit(1);
    }

    luaL_openlibs(L); // Open all standard Lua libraries (base, table, io, string, math, debug etc.)

    // Expose Editor API to Lua
    exposeEditorToLua();

    statusMessage = "Lua interpreter initialized.";
    statusMessageTime = GetTickCount64();
}

void Editor::finalizeLua() {
    if (L) {
        lua_close(L); // Close Lua state, cleans up everything
        L = nullptr;
    }
    statusMessage = "Lua interpreter finalized.";
    statusMessageTime = GetTickCount64();
}

void Editor::exposeEditorToLua() {
    // Create a new table (like a Python module)
    lua_newtable(L);
    int editor_api_table_idx = lua_gettop(L); // Get its index on the stack

    // Push the 'this' pointer to Editor onto the stack as userdata.
    // This userdata will be used as an 'upvalue' for all functions in editor_lib.
    lua_pushlightuserdata(L, this); // 'light' means Lua doesn't manage its memory

    // Register all C functions in editor_lib into the new table
    // For each function in editor_lib, lua_pushcclosure pushes the C function
    // along with 1 upvalue (the Editor* userdata)
    luaL_setfuncs(L, editor_lib, 1);

    // Set the table as a global variable named 'editor_api'
    lua_setglobal(L, "editor_api");

    // Add plugin directory to Lua's package.path
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "path"); // Get package.path
    std::string current_path = lua_tostring(L, -1);
    current_path += ";./plugins/?.lua"; // Add plugins dir relative to executable
    lua_pop(L, 1); // Pop old path
    lua_pushstring(L, current_path.c_str());
    lua_setfield(L, -2, "path"); // Set new path
    lua_pop(L, 1); // Pop package table

    // Check for errors
    if (lua_status(L) != LUA_OK) {
        std::cerr << "Error exposing Editor to Lua: " << lua_tostring(L, -1) << std::endl;
        lua_pop(L, 1); // Pop error message
    }
}

void Editor::loadLuaPlugins(const std::string& pluginDir) {
    if (!L) {
        statusMessage = "Lua interpreter not initialized, cannot load plugins.";
        statusMessageTime = GetTickCount64();
        return;
    }
    statusMessage = "Loading Lua plugins from '" + pluginDir + "'...";
    statusMessageTime = GetTickCount64();

    CreateDirectoryA(pluginDir.c_str(), NULL); // Ensure plugins directory exists

    WIN32_FIND_DATAA findFileData;
    HANDLE hFind = FindFirstFileA((pluginDir + "\\*.lua").c_str(), &findFileData);

    if (hFind == INVALID_HANDLE_VALUE) {
        statusMessage = "No Lua plugins found in '" + pluginDir + "'";
        statusMessageTime = GetTickCount64();
        return;
    }

    do {
        std::string luaFileName = findFileData.cFileName;
        if (luaFileName == "." || luaFileName == "..") continue;

        std::string luaFilePath = pluginDir + "\\" + luaFileName;

        // Load and execute the Lua script
        int status = luaL_dofile(L, luaFilePath.c_str());
        if (status != LUA_OK) {
            std::string error_msg = lua_tostring(L, -1);
            lua_pop(L, 1); // Pop error message
            statusMessage = "Error loading Lua plugin '" + luaFileName + "': " + error_msg;
            statusMessageTime = GetTickCount64();
            std::cerr << "Lua Error: " << error_msg << std::endl;
        } else {
            statusMessage = "Loaded Lua plugin: " + luaFileName;
            statusMessageTime = GetTickCount64();

            // Optional: Call an 'on_load' function in the plugin if it exists
            lua_getglobal(L, "on_load"); // Try to get a global 'on_load' function from the plugin
            if (lua_isfunction(L, -1)) {
                // Call it (no arguments)
                status = lua_pcall(L, 0, 0, 0); // 0 args, 0 results, 0 error func
                if (status != LUA_OK) {
                    std::string error_msg = lua_tostring(L, -1);
                    lua_pop(L, 1);
                    statusMessage = "Error in plugin 'on_load': " + error_msg;
                    statusMessageTime = GetTickCount64();
                    std::cerr << "Lua on_load Error: " << error_msg << std::endl;
                }
            } else {
                lua_pop(L, 1); // Pop nil if on_load not found
            }
        }
    } while (FindNextFileA(hFind, &findFileData) != 0);

    FindClose(hFind);
    statusMessage = "Finished loading Lua plugins.";
    statusMessageTime = GetTickCount64();
}

bool Editor::executeLuaPluginCommand(const std::string& pluginName, const std::string& commandName) {
    if (!L) {
        statusMessage = "Lua interpreter not initialized, cannot execute plugin command.";
        statusMessageTime = GetTickCount64();
        return false;
    }
    
    // Attempt to get the function from the global scope (assuming plugins define global functions)
    // Or if plugins create a table/module, you'd do:
    // lua_getglobal(L, pluginName.c_str());
    // if (!lua_istable(L, -1)) { lua_pop(L, 1); return false; } // Not a table
    // lua_getfield(L, -1, commandName.c_str());
    // lua_remove(L, -2); // Pop the table
    
    // For simplicity, let's assume `commandName` is a global function directly
    lua_getglobal(L, commandName.c_str()); // Push function onto stack

    if (!lua_isfunction(L, -1)) {
        std::string type_name = lua_typename(L, lua_type(L, -1));
        lua_pop(L, 1); // Pop non-function value
        statusMessage = "Command '" + commandName + "' not found or not a function in Lua. Type was: " + type_name;
        statusMessageTime = GetTickCount64();
        return false;
    }

    // Call the Lua function (0 args, 0 results, 0 error handler)
    int status = lua_pcall(L, 0, 0, 0);
    if (status != LUA_OK) {
        std::string error_msg = lua_tostring(L, -1);
        lua_pop(L, 1); // Pop error message
        statusMessage = "Error executing Lua command '" + commandName + "': " + error_msg;
        statusMessageTime = GetTickCount64();
        std::cerr << "Lua Command Error: " << error_msg << std::endl;
        return false;
    }

    statusMessage = "Lua command '" + commandName + "' executed.";
    statusMessageTime = GetTickCount64();
    return true;
}

void Editor::calculateLineNumberWidth()
{
    size_t effectiveLineNumbers = std::max(static_cast<size_t>(1), lines.size());
    int numLines = static_cast<int>(effectiveLineNumbers);

    int digits = 0;
    if (numLines == 0) digits = 1;
    else {
        int temp = numLines;
        while (temp > 0) {
            temp /= 10;
            ++digits;
        }
    }
    lineNumberWidth = digits + 1;
}

void Editor::updateScreenSize() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi); // Use GetStdHandle locally
    int newScreenRows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    int newScreenCols = csbi.srWindow.Right - csbi.srWindow.Left + 1;

    if (newScreenRows != screenRows || newScreenCols != screenCols)
    {
        screenRows = newScreenRows;
        screenCols = newScreenCols;
        calculateLineNumberWidth();

        prevDrawnLines.resize(screenRows - 2);
        for (auto& s : prevDrawnLines)
        {
            if (s.length() != screenCols)
            {
                s.assign(screenCols, ' ');
            }
        }
        clearScreen();
        prevStatusMessage = "";
        prevMessageBarMessage = "";
    }
}

void Editor::setCursorVisibility(bool visible) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO cursorInfo;
    if (!GetConsoleCursorInfo(hConsole, &cursorInfo)) {
        return;
    }
    cursorInfo.bVisible = visible;
    SetConsoleCursorInfo(hConsole, &cursorInfo);
}

void Editor::drawScreenContent() {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole == INVALID_HANDLE_VALUE) {
        std::cerr << "Error: Invalid console handle in drawScreenContent." << std::endl;
        return;
    }

    // This is the effective drawing width for text content after line numbers
    int effectiveScreenCols = screenCols - lineNumberWidth;

    for (int i = 0; i < screenRows - 2; ++i) { // Iterate through each visible screen row for content
        int fileRow = rowOffset + i;
        std::string fullLineContentToDraw = ""; // The full string content of the screen line (chars)
        std::vector<WORD> fullLineAttributes(screenCols); // Attributes for each char on the screen line

        // --- 1. Prepare Line Number Part ---
        std::string lineNumberStr;
        std::ostringstream ss_lineNumber;
        if (fileRow >= 0 && fileRow < lines.size()) { // Check bounds for actual lines
            ss_lineNumber << std::setw(lineNumberWidth - 1) << (fileRow + 1) << " ";
        } else {
            // For lines beyond file content (like ~ in Vim)
            ss_lineNumber << std::string(lineNumberWidth - 1, ' ') << "~";
        }
        lineNumberStr = ss_lineNumber.str();
        fullLineContentToDraw += lineNumberStr;

        // Set attributes for the line number part
        for (int k = 0; k < lineNumberStr.length(); ++k) {
            fullLineAttributes[k] = defaultFgColor | defaultBgColor; // Default text color for line numbers
        }


        // --- 2. Prepare Rendered Text Content Part ---
        std::string renderedTextContent = "";
        if (fileRow >= 0 && fileRow < lines.size()) {
            std::string fullRenderedLine = getRenderedLine(fileRow);

            int startCharInRenderedLine = colOffset;
            int endCharInRenderedLine = colOffset + effectiveScreenCols;

            // Clamp the substring indices to valid range
            startCharInRenderedLine = std::max(0, startCharInRenderedLine);
            startCharInRenderedLine = std::min(startCharInRenderedLine, (int)fullRenderedLine.length());
            endCharInRenderedLine = std::max(0, endCharInRenderedLine);
            endCharInRenderedLine = std::min(endCharInRenderedLine, (int)fullRenderedLine.length());

            if (endCharInRenderedLine > startCharInRenderedLine) {
                renderedTextContent = fullRenderedLine.substr(startCharInRenderedLine, endCharInRenderedLine - startCharInRenderedLine);
            }
        }
        fullLineContentToDraw += renderedTextContent;

        // Set attributes for the main text content, including search highlighting
        int currentScreenCol = lineNumberStr.length(); // Start after line numbers
        for (int k = 0; k < renderedTextContent.length(); ++k) {
            WORD current_attributes = defaultFgColor | defaultBgColor; // Base text color
            bool isHighlight = false;

            // Check if current character is part of the highlighted search result
            if (!searchQuery.empty() && currentMatchIndex != -1 && fileRow == searchResults[currentMatchIndex].first) {
                // Convert search result's character index (in logical line) to its rendered/visual index
                int matchLogicalStart = searchResults[currentMatchIndex].second;
                int matchLogicalEnd = searchResults[currentMatchIndex].second + searchQuery.length();

                // Get rendered positions of match in the *full* rendered line (not just visible part)
                int matchRenderedStart = cxToRx(fileRow, matchLogicalStart);
                int matchRenderedEnd = cxToRx(fileRow, matchLogicalEnd);

                // Calculate the character's *global rendered position* on the line being drawn
                // This is (currentScreenCol - lineNumberStr.length()) + colOffset (where colOffset is the scroll)
                int charGlobalRenderedPos = (currentScreenCol + k) - lineNumberStr.length() + colOffset;

                // If this character's global rendered position falls within the highlighted match's rendered range
                if (charGlobalRenderedPos >= matchRenderedStart && charGlobalRenderedPos < matchRenderedEnd) {
                    isHighlight = true;
                }
            }

            if (isHighlight) {
                current_attributes = BG_YELLOW | BLACK; // Highlight: Black text on Yellow background
            }
            fullLineAttributes[currentScreenCol + k] = current_attributes;
        }

        // --- 3. Pad the rest of the line with spaces and default attributes ---
        for (int k = fullLineContentToDraw.length(); k < screenCols; ++k) {
            fullLineContentToDraw += ' ';
            fullLineAttributes[k] = defaultFgColor | defaultBgColor;
        }


        // --- 4. Perform Diff and Draw Only if Necessary ---
        // Compare the *full* line string (including padding) with the previously drawn line
        // We ensure prevDrawnLines is appropriately sized by updateScreenSize()
        if (i >= prevDrawnLines.size() || prevDrawnLines[i] != fullLineContentToDraw) {
            COORD writePos = { 0, (SHORT)i }; // Position to start writing on the console buffer
            DWORD charsWritten;

            // Write characters to the console
            if (!WriteConsoleOutputCharacterA(hConsole, fullLineContentToDraw.c_str(), screenCols, writePos, &charsWritten)) {
                std::cerr << "WriteConsoleOutputCharacterA failed: " << GetLastError() << std::endl;
            }

            // Write attributes to the console
            if (!WriteConsoleOutputAttribute(hConsole, fullLineAttributes.data(), screenCols, writePos, &charsWritten)) {
                std::cerr << "WriteConsoleOutputAttribute failed: " << GetLastError() << std::endl;
            }

            // Update the cached line for the next diff cycle
            if (i < prevDrawnLines.size()) {
                prevDrawnLines[i] = fullLineContentToDraw;
            }
        }
    }

    // Status and message bars are drawn separately (and more simply, so they can use direct calls)
    drawStatusBar();
    drawMessageBar();
}

void Editor::startSearch() {
    originalCursorX = cursorX; // Save original cursor position
    originalCursorY = cursorY;
    originalRowOffset = rowOffset;
    originalColOffset = colOffset;

    mode = PROMPT_MODE;
    promptMessage = "Search: ";
    searchQuery = ""; // Clear previous search query
    searchResults.clear(); // Clear previous results
    currentMatchIndex = -1; // No match selected yet
    statusMessage = "Enter search term. ESC to cancel, Enter to search.";
    statusMessageTime = GetTickCount64();

    // Invalidate prevDrawnLines to ensure prompt is drawn fresh
    for(auto& s : prevDrawnLines) s.assign(screenCols, ' ');
    prevStatusMessage = "";
    prevMessageBarMessage = "";
    clearScreen(); // Force full screen redraw to show prompt cleanly
}

void Editor::performSearch() {
    searchResults.clear();
    currentMatchIndex = -1;

    if (searchQuery.empty()) {
        statusMessage = "Search cancelled or empty.";
        statusMessageTime = GetTickCount64();
        mode = EDIT_MODE; // Exit search mode if query is empty
        cursorX = originalCursorX; // Restore cursor
        cursorY = originalCursorY;
        rowOffset = originalRowOffset; // Restore scroll
        colOffset = originalColOffset;
        return;
    }

    // Iterate through lines to find all matches
    for (int r = 0; r < lines.size(); ++r) {
        size_t pos = lines[r].find(searchQuery, 0);
        while (pos != std::string::npos) {
            searchResults.push_back({r, (int)pos});
            pos = lines[r].find(searchQuery, pos + 1); // Find next occurrence after current match
        }
    }

    if (searchResults.empty()) {
        statusMessage = "No matches found for '" + searchQuery + "'";
        statusMessageTime = GetTickCount64();
        mode = EDIT_MODE; // No matches, go back to edit mode
        cursorX = originalCursorX; // Restore cursor
        cursorY = originalCursorY;
        rowOffset = originalRowOffset; // Restore scroll
        colOffset = originalColOffset;
        return;
    }

    // Found matches. Go to the first one.
    currentMatchIndex = 0;
    statusMessage = "Found " + std::to_string(searchResults.size()) + " matches. (N)ext (P)rev";
    statusMessageTime = GetTickCount64();

    // Move cursor and scroll to the first match
    cursorY = searchResults[currentMatchIndex].first;
    cursorX = searchResults[currentMatchIndex].second;
    
    // Adjust scroll to make sure the found match is visible
    scroll(); // This should adjust rowOffset/colOffset
    
    mode = EDIT_MODE; // Exit search prompt mode
}


void Editor::findNext() {
    if (searchResults.empty()) return;

    currentMatchIndex = (currentMatchIndex + 1) % searchResults.size();

    // Move cursor and scroll to the new match
    cursorY = searchResults[currentMatchIndex].first;
    cursorX = searchResults[currentMatchIndex].second;
    scroll(); // Adjust scroll

    statusMessage = "Match " + std::to_string(currentMatchIndex + 1) + " of " + std::to_string(searchResults.size());
    statusMessageTime = GetTickCount64();
}

void Editor::findPrevious() {
    if (searchResults.empty()) return;

    currentMatchIndex--;
    if (currentMatchIndex < 0) {
        currentMatchIndex = searchResults.size() - 1;
    }

    cursorY = searchResults[currentMatchIndex].first;
    cursorX = searchResults[currentMatchIndex].second;
    scroll();

    statusMessage = "Match " + std::to_string(currentMatchIndex + 1) + " of " + std::to_string(searchResults.size());
    statusMessageTime = GetTickCount64();
}

bool Editor::promptUser(const std::string& prompt, int input_c, std::string& result) {
    if (input_c == 13) { // Enter
        mode = EDIT_MODE; // Exit prompt mode
        return true; // Success
    } else if (input_c == 27) { // Escape
        mode = EDIT_MODE; // Exit prompt mode
        result.clear(); // Clear the input
        statusMessage = "Operation cancelled.";
        statusMessageTime = GetTickCount64();
        cursorX = originalCursorX; // Restore cursor
        cursorY = originalCursorY;
        rowOffset = originalRowOffset; // Restore scroll
        colOffset = originalColOffset;
        return false; // Cancelled
    } else if (input_c == 8) { // Backspace
        if (!result.empty()) {
            result.pop_back();
        }
    } else if (input_c >= 32 && input_c <= 126) { // Printable characters
        result += static_cast<char>(input_c);
    }
    // Update the message bar with the current prompt and input
    statusMessage = prompt + result;
    statusMessageTime = GetTickCount64(); // Keep message alive while typing

    // Return false to indicate prompt mode is still active
    return false;
}

void Editor::drawStatusBar() {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole == INVALID_HANDLE_VALUE) return;

    std::string currentStatus;
    std::string filename_display = filename.empty() ? "[No Name]" : filename;
    currentStatus += filename_display;
    currentStatus += " - " + std::to_string(lines.size()) + " lines";

    std::string cursor_info = std::to_string(cursorY + 1) + "/" + std::to_string(lines.size());
    std::string spaces(screenCols - currentStatus.length() - cursor_info.length(), ' ');

    currentStatus += spaces + cursor_info;

    if (currentStatus.length() < screenCols) {
        currentStatus += std::string(screenCols - currentStatus.length(), ' ');
    } else if (currentStatus.length() > screenCols) {
        currentStatus = currentStatus.substr(0, screenCols);
    }

    if (currentStatus != prevStatusMessage) {
        std::vector<WORD> attributes(screenCols);
        for (int k = 0; k < screenCols; ++k) {
            attributes[k] = BG_BLUE | WHITE;
        }

        COORD writePos = {0, (SHORT)(screenRows - 2)};
        DWORD charsWritten;

        WriteConsoleOutputCharacterA(hConsole, currentStatus.c_str(), screenCols, writePos, &charsWritten);
        WriteConsoleOutputAttribute(hConsole, attributes.data(), screenCols, writePos, &charsWritten);

        prevStatusMessage = currentStatus;
    }
}

void Editor::drawMessageBar() {
    std::string currentMessage = "";
    ULONGLONG currentTime = GetTickCount64();
    if (currentTime - statusMessageTime < 5000) { // Display for 5 seconds
        currentMessage = trimRight(statusMessage);
    }
    currentMessage += std::string(screenCols - currentMessage.length(), ' ');

    // --- Only redraw if message bar content has changed ---
    if (currentMessage != prevMessageBarMessage) {
        setCursorPosition(0, screenRows - 1); // Last row
        setTextColor(YELLOW | INTENSITY);
        writeStringAt(0, screenRows - 1, currentMessage);
        prevMessageBarMessage = currentMessage;
    }
}

// Helper to trim trailing spaces (useful for status message display)
std::string Editor::trimRight(const std::string& s) {
    size_t end = s.find_last_not_of(" \t\n\r\f\v");
    return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}

void Editor::scroll() {
    if (cursorY < rowOffset) {
        rowOffset = cursorY;
        force_full_redraw_internal();
    }
    if (cursorY >= rowOffset + screenRows - 2) {
        rowOffset = cursorY - (screenRows - 2) + 1;
        force_full_redraw_internal();
    }

    int renderedCursorX = cxToRx(cursorY, cursorX); // Get visual position of cursor
    int effectiveColsForText = screenCols - lineNumberWidth;

    if (renderedCursorX < colOffset) {
        colOffset = renderedCursorX;
        force_full_redraw_internal();
    }
    if (renderedCursorX >= colOffset + effectiveColsForText) {
        colOffset = renderedCursorX - effectiveColsForText + 1;
        force_full_redraw_internal();
    }
}

void Editor::moveCursor(int key) {
    int oldCursorX = cursorX;
    int oldCursorY = cursorY;
    std::string currentLine = (cursorY < lines.size()) ? lines[cursorY] : lines.front();

    switch (key) {
    case ARROW_LEFT:
        if (cursorX > 0) {
            // To move left, decrement cursorX.
            // If the character at cursorX-1 is a tab,
            // this is fine for internal storage.
            cursorX--;
        }
        else if (cursorY > 0) {
            cursorY--;
            cursorX = lines[cursorY].length(); // Move to end of previous line (char index)
        }
        break;
    case ARROW_RIGHT:
        if (cursorY < lines.size() && cursorX < currentLine.length()) {
            // To move right, increment cursorX.
            // This correctly moves to the next character index.
            cursorX++;
        }
        else if (cursorY < lines.size() - 1) { // Move to beginning of next line
            cursorY++;
            cursorX = 0;
        }
        break;
    case ARROW_UP:
        if (cursorY > 0) {
            // When moving up/down, we want the cursor to try and stay at the
            // *same visual column* if possible.
            // So, first convert current cx to rx, then convert that rx to new cx on target line.
            int currentRx = cxToRx(cursorY, cursorX);
            cursorY--;
            if (cursorY < lines.size()) {
                cursorX = rxToCx(cursorY, currentRx); // Find the character index closest to the current rendered column
            }
            else {
                cursorX = 0; // If somehow moved past valid lines, reset
            }
        }
        break;
    case ARROW_DOWN:
        if (cursorY < lines.size() - 1) {
            // Same logic as ARROW_UP
            int currentRx = cxToRx(cursorY, cursorX);
            cursorY++;
            if (cursorY < lines.size()) {
                cursorX = rxToCx(cursorY, currentRx); // Find the character index closest to the current rendered column
            }
            else {
                cursorX = 0;
            }
        }
        break;
    case HOME_KEY:
        cursorX = 0; // Always character index 0
        break;
    case END_KEY:
        if (cursorY < lines.size()) {
            cursorX = lines[cursorY].length(); // Always end of character string
        }
        break;
    case PAGE_UP:
    case PAGE_DOWN: {
        int currentRx = cxToRx(cursorY, cursorX); // Store current rendered X before jump
        int times = screenRows - 2;
        if (key == PAGE_UP) {
            cursorY = std::max(0, cursorY - times);
        }
        else { // PAGE_DOWN
            cursorY = std::min((int)lines.size() - 1, cursorY + times);
        }
        // After changing Y, re-calculate X based on old rendered position
        if (cursorY < lines.size()) {
            cursorX = rxToCx(cursorY, currentRx);
        }
        else if (lines.empty()) { // Edge case: empty file, moved page
            cursorX = 0;
            cursorY = 0;
        }
        else { // Landed on an invalid line, default to end of last line
            cursorY = lines.size() - 1;
            cursorX = lines[cursorY].length();
        }
        break;
    }
    }
}

void Editor::insertChar(int c) {
    if (cursorY == lines.size()) {
        lines.push_back("");
    }
    lines[cursorY].insert(cursorX, 1, static_cast<char>(c));
    cursorX++;
    statusMessage = "";
    statusMessageTime = 0; // Clear immediately
    calculateLineNumberWidth();
    dirty = true;
}

void Editor::insertNewline() {
    if (cursorX == lines[cursorY].length()) {
        lines.insert(lines.begin() + cursorY + 1, "");
    }
    else {
        std::string remaining = lines[cursorY].substr(cursorX);
        lines[cursorY].erase(cursorX);
        lines.insert(lines.begin() + cursorY + 1, remaining);
    }
    cursorY++;
    cursorX = 0;
    calculateLineNumberWidth();
    statusMessage = "";
    statusMessageTime = 0;
    dirty = true;
}

void Editor::deleteChar() { // Backspace
    if (cursorY == lines.size()) return;
    if (cursorX == 0 && cursorY == 0 && lines[0].empty()) {
        return;
    }

    if (cursorX > 0) {
        lines[cursorY].erase(cursorX - 1, 1);
        cursorX--;
    }
    else {
        cursorX = lines[cursorY - 1].length();
        lines[cursorY - 1] += lines[cursorY];
        lines.erase(lines.begin() + cursorY);
        cursorY--;
        calculateLineNumberWidth();
    }
    statusMessage = "";
    statusMessageTime = 0;
    dirty = true;
}

void Editor::deleteForwardChar() { // Delete key
    if (cursorY == lines.size()) return;
    if (cursorX == lines[cursorY].length() && cursorY == lines.size() - 1) {
        return;
    }

    if (cursorX < lines[cursorY].length()) {
        lines[cursorY].erase(cursorX, 1);
    }
    else {
        lines[cursorY] += lines[cursorY + 1];
        lines.erase(lines.begin() + cursorY + 1);
        calculateLineNumberWidth();
    }
    statusMessage = "";
    statusMessageTime = 0;
    dirty = true;
}


bool Editor::openFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        statusMessage = "Error: Could not open file '" + path + "'";
        statusMessageTime = GetTickCount64();
        return false;
    }

    lines.clear();
    std::string line;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    file.close();

    if (lines.empty()) {
        lines.push_back("");
    }

    filename = path;
    cursorX = 0;
    cursorY = 0;
    rowOffset = 0;
    colOffset = 0;
    calculateLineNumberWidth();
    statusMessage = "Opened '" + path + "'";
    statusMessageTime = GetTickCount64();

    // Force a full redraw after file open
    for (auto& s : prevDrawnLines) s.assign(screenCols, ' ');
    prevStatusMessage = "";
    prevMessageBarMessage = "";

    dirty = false;
    return true;
}

bool Editor::saveFile() {
    if (filename.empty()) {
        statusMessage = "Cannot save: No filename specified. Use Ctrl-O to open/create a file.";
        statusMessageTime = GetTickCount64();
        return false;
    }

    std::ofstream file(filename);
    if (!file.is_open()) {
        statusMessage = "Error: Could not save file '" + filename + "'";
        statusMessageTime = GetTickCount64();
        dirty = false;
        return false;
    }

    for (const auto& line : lines) {
        file << line << "\r\n"; // Windows line endings (CRLF)
    }
    file.close();

    statusMessage = "Saved '" + filename + "' (" + std::to_string(lines.size()) + " lines)";
    statusMessageTime = GetTickCount64();
    return true;
}

int Editor::cxToRx(int lineIndex, int cx)
{
    if (lineIndex < 0 || lineIndex >= lines.size()) return 0;

    int rx = 0;
    const std::string& line = lines[lineIndex];
    for (int i = 0; i < cx; ++i) {
        if (line[i] == '\t') {
            rx += (KILO_TAB_STOP - (rx % KILO_TAB_STOP));
        }
        else {
            ++rx;
        }
    }
    return rx;
}

int Editor::rxToCx(int lineIndex, int rx)
{
    if (lineIndex < 0 || lineIndex >= lines.size()) return 0;

    int currentRx = 0;
    int cx = 0;
    const std::string& line = lines[lineIndex];
    for (int i = 0; i < line.length(); ++i) {
        if (line[i] == '\t') {
            currentRx += (KILO_TAB_STOP - (currentRx % KILO_TAB_STOP));
        }
        else {
            currentRx++;
        }
        if (currentRx > rx) return cx;
        ++cx;
    }
    return cx;
}

std::string Editor::getRenderedLine(int fileRow)
{
    if (fileRow < 0 || fileRow >= lines.size()) {
        return "";
    }
    std::string renderedLine;
    const std::string& originalLine = lines[fileRow];
    for (char c : originalLine) {
        if (c == '\t') {
            int spacesToAdd = KILO_TAB_STOP - (renderedLine.length() % KILO_TAB_STOP);
            renderedLine.append(spacesToAdd, ' ');
        }
        else {
            renderedLine += c;
        }
    }
    return renderedLine;
}

void Editor::toggleFileExplorer() {
    if (mode == EDIT_MODE) {
        mode = FILE_EXPLORER_MODE;
        statusMessage = "File Explorer Mode: Navigate with arrows, Enter to open/CD, N for New, D for Delete.";
        statusMessageTime = GetTickCount64();
        populateDirectoryEntries(currentDirPath); // Populate entries when entering mode
        selectedFileIndex = 0; // Reset selection
        fileExplorerScrollOffset = 0; // Reset scroll
        clearScreen(); // Force full redraw when changing modes
        for (auto& s : prevDrawnLines) s.assign(screenCols, ' '); // Invalidate text editor view
    }
    else { // FILE_EXPLORER_MODE
        mode = EDIT_MODE;
        statusMessage = "Edit Mode: Ctrl-Q = quit | Ctrl-S = save | Ctrl-O = open | Ctrl-E = explorer";
        statusMessageTime = GetTickCount64();
        directoryEntries.clear(); // Clear entries when exiting
        clearScreen(); // Force full redraw when changing modes
        for (auto& s : prevDrawnLines) s.assign(screenCols, ' '); // Invalidate text editor view
    }
    prevStatusMessage = ""; // Force status bar redraw
    prevMessageBarMessage = ""; // Force message bar redraw
}

void Editor::populateDirectoryEntries(const std::string& path) {
    directoryEntries.clear();

    // Add ".." entry for navigating up
    directoryEntries.push_back({ "..", true });

    WIN32_FIND_DATAA findFileData; // Using A for ANSI string
    HANDLE hFind = FindFirstFileA((path + "\\*").c_str(), &findFileData); // Find files and directories

    if (hFind == INVALID_HANDLE_VALUE) {
        statusMessage = "Error: Could not list directory '" + path + "'";
        statusMessageTime = GetTickCount64();
        return;
    }

    do {
        std::string entryName = findFileData.cFileName;
        // Skip "." and ".." as we handle ".." explicitly
        if (entryName == "." || entryName == "..") {
            continue;
        }

        bool isDir = (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        directoryEntries.push_back({ entryName, isDir });

    } while (FindNextFileA(hFind, &findFileData) != 0);

    FindClose(hFind);

    // Sort entries: directories first, then files, both alphabetically
    std::sort(directoryEntries.begin() + 1, directoryEntries.end(), [](const DirEntry& a, const DirEntry& b) {
        if (a.isDirectory && !b.isDirectory) return true;
        if (!a.isDirectory && b.isDirectory) return false;
        return a.name < b.name;
        });

    currentDirPath = path; // Update current path
    statusMessage = "Viewing: " + currentDirPath;
    statusMessageTime = GetTickCount64();
}

void Editor::moveFileExplorerSelection(int key) {
    if (directoryEntries.empty()) return;

    int oldSelectedFileIndex = selectedFileIndex;
    int oldFileExplorerScrollOffset = fileExplorerScrollOffset; // Capture old scroll for potential diffing

    if (key == ARROW_UP) {
        selectedFileIndex = std::max(0, selectedFileIndex - 1);
    }
    else if (key == ARROW_DOWN) {
        selectedFileIndex = std::min((int)directoryEntries.size() - 1, selectedFileIndex + 1);
    }
    else if (key == PAGE_UP) {
        selectedFileIndex = std::max(0, selectedFileIndex - (screenRows - 4));
    }
    else if (key == PAGE_DOWN) {
        selectedFileIndex = std::min((int)directoryEntries.size() - 1, selectedFileIndex + (screenRows - 4));
    }
    else if (key == HOME_KEY) {
        selectedFileIndex = 0;
    }
    else if (key == END_KEY) {
        selectedFileIndex = directoryEntries.size() - 1;
    }

    // Adjust scroll offset
    if (selectedFileIndex < fileExplorerScrollOffset) {
        fileExplorerScrollOffset = selectedFileIndex;
    }
    if (selectedFileIndex >= fileExplorerScrollOffset + (screenRows - 4)) {
        fileExplorerScrollOffset = selectedFileIndex - (screenRows - 4) + 1;
    }

    if (oldSelectedFileIndex != selectedFileIndex || oldFileExplorerScrollOffset != fileExplorerScrollOffset) {
        force_full_redraw_internal(); // This now just invalidates the cache, doesn't clear screen
    }
}

void Editor::handleFileExplorerEnter() {
    if (directoryEntries.empty() || selectedFileIndex < 0 || selectedFileIndex >= directoryEntries.size()) {
        return; // Nothing selected
    }

    DirEntry& selectedEntry = directoryEntries[selectedFileIndex];

    char newPathBuffer[MAX_PATH];
    if (PathCombineA(newPathBuffer, currentDirPath.c_str(), selectedEntry.name.c_str()) == NULL) {
        statusMessage = "Error: Invalid path combination.";
        statusMessageTime = GetTickCount64();
        return;
    }
    std::string newPath = newPathBuffer;

    if (selectedEntry.isDirectory) {
        // Change directory
        populateDirectoryEntries(newPath);
        selectedFileIndex = 0; // Reset selection in new directory
        fileExplorerScrollOffset = 0;
        clearScreen(); // Full refresh after CD
    }
    else {
        // Open file in editor mode
        openFile(newPath);
        toggleFileExplorer(); // Switch back to edit mode
    }
}

void Editor::drawFileExplorer() {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole == INVALID_HANDLE_VALUE) return;

    int startRowForHeader = 0;
    int startRowForContent = startRowForHeader + 2;
    int visibleRowsForContent = screenRows - startRowForContent - 2;

    std::string pathLine = "PATH: " + currentDirPath;
    if (pathLine.length() < screenCols) {
        pathLine += std::string(screenCols - pathLine.length(), ' ');
    } else if (pathLine.length() > screenCols) {
        pathLine = pathLine.substr(0, screenCols);
    }

    std::vector<WORD> pathAttrs(screenCols);
    for(int k=0; k<screenCols; ++k) pathAttrs[k] = CYAN | INTENSITY | defaultBgColor;

    COORD pathWritePos = {0, (SHORT)startRowForHeader};
    DWORD charsWritten;
    WriteConsoleOutputCharacterA(hConsole, pathLine.c_str(), screenCols, pathWritePos, &charsWritten);
    WriteConsoleOutputAttribute(hConsole, pathAttrs.data(), screenCols, pathWritePos, &charsWritten);

    std::string blankLine(screenCols, ' ');
    std::vector<WORD> blankAttrs(screenCols, defaultFgColor | defaultBgColor);
    COORD blankLinePos = {0, (SHORT)(startRowForHeader + 1)};
    WriteConsoleOutputCharacterA(hConsole, blankLine.c_str(), screenCols, blankLinePos, &charsWritten);
    WriteConsoleOutputAttribute(hConsole, blankAttrs.data(), screenCols, blankLinePos, &charsWritten);

    for (int i = 0; i < visibleRowsForContent; ++i) {
        int entryIndex = fileExplorerScrollOffset + i;
        int currentScreenRow = startRowForContent + i;

        std::string lineContentToDraw = "";
        std::vector<WORD> lineAttributes(screenCols);

        if (entryIndex >= 0 && entryIndex < directoryEntries.size()) {
            const DirEntry& entry = directoryEntries[entryIndex];

            std::string prefix = (entryIndex == selectedFileIndex) ? "> " : "  ";
            lineContentToDraw += prefix;
            WORD prefixBg = defaultBgColor;
            WORD prefixFg = defaultFgColor;
            if (entryIndex == selectedFileIndex) {
                prefixBg = BG_BLUE;
                prefixFg = WHITE | INTENSITY;
            }
            for(int k=0; k<prefix.length(); ++k) lineAttributes[k] = prefixFg | prefixBg;


            std::string formattedEntryName;
            if (entry.isDirectory) {
                formattedEntryName = "[" + entry.name + "]";
            } else {
                formattedEntryName = " " + entry.name;
            }
            lineContentToDraw += formattedEntryName;

            WORD entryBg = defaultBgColor;
            WORD entryFg = defaultFgColor;
            if (entryIndex == selectedFileIndex) {
                entryBg = BG_BLUE;
                entryFg = WHITE | INTENSITY;
            } else if (entry.isDirectory) {
                entryFg = BLUE | INTENSITY;
            } else {
                entryFg = WHITE;
            }
            for(int k=0; k<formattedEntryName.length(); ++k) {
                lineAttributes[prefix.length() + k] = entryFg | entryBg;
            }

        } else {
            lineContentToDraw = "";
            for(int k=0; k<screenCols; ++k) lineAttributes[k] = defaultFgColor | defaultBgColor;
        }

        for (int k = lineContentToDraw.length(); k < screenCols; ++k) {
            lineContentToDraw += ' ';
            if (k >= lineAttributes.size()) {
                lineAttributes.push_back(defaultFgColor | defaultBgColor);
            } else {
                lineAttributes[k] = defaultFgColor | defaultBgColor;
            }
        }
        
        COORD writePos = {0, (SHORT)currentScreenRow};
        WriteConsoleOutputCharacterA(hConsole, lineContentToDraw.c_str(), screenCols, writePos, &charsWritten);
        WriteConsoleOutputAttribute(hConsole, lineAttributes.data(), screenCols, writePos, &charsWritten);
    }

    for (int i = startRowForContent + visibleRowsForContent; i < screenRows - 2; ++i) {
        std::string emptyLine(screenCols, ' ');
        std::vector<WORD> emptyAttrs(screenCols, defaultFgColor | defaultBgColor);
        COORD writePos = {0, (SHORT)i};
        WriteConsoleOutputCharacterA(hConsole, emptyLine.c_str(), screenCols, writePos, &charsWritten);
        WriteConsoleOutputAttribute(hConsole, emptyAttrs.data(), screenCols, writePos, &charsWritten);
    }
}

// Terminal
void Editor::toggleTerminal() {
    if (mode == TERMINAL_MODE) {
        mode = EDIT_MODE;
        stopTerminal();
        statusMessage = "Edit Mode: Ctrl-Q = quit | Ctrl-S = save | Ctrl-o = open | Ctrl-E = fileexplorer";
        statusMessageTime = GetTickCount64();
    } else {
        mode = TERMINAL_MODE;
        startTerminal();
        statusMessage = "Temrinal Mode: Ctrl-T to close";
        statusMessageTime = GetTickCount64();
    }
    clearScreen();
    for (auto& s : prevDrawnLines) s.assign(screenCols, ' ');
    prevStatusMessage = "";
    prevMessageBarMessage = "";
}

void Editor::startTerminal() {
    if (piProcInfo.hProcess != NULL) {
        stopTerminal();
    }

    ZeroMemory(&piProcInfo, sizeof(piProcInfo));

    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);   
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    // create pipe for stdout.
    if (!CreatePipe(&hChildStdoutRead, &hChildStdoutWrite, &saAttr, 0)) {
        statusMessage = "Error: Stdout pipe creation failed. (GLE: " + std::to_string(GetLastError()) + ")";
        statusMessageTime = GetTickCount64(); 
        return;
    }
    // ensure the read handle is not inherited.
    if (!SetHandleInformation(hChildStdoutRead, HANDLE_FLAG_INHERIT, 0)) {
        statusMessage = "Error: Stdin pipe creation failed. (GLE: " + std::to_string(GetLastError()) + ")";
        statusMessageTime = GetTickCount64(); 
        return;
    }

    // Create a pipe for stdin
    if (!CreatePipe(&hChildStdinRead, &hChildStdinWrite, &saAttr, 0)) {
        statusMessage = "Error: Stdin pipe creation failed. (GLE: " + std::to_string(GetLastError()) = ")";
        statusMessageTime = GetTickCount64();
        CloseHandle(hChildStdinRead); 
        CloseHandle(hChildStdinWrite);
        CloseHandle(hChildStdoutRead); 
        CloseHandle(hChildStdoutWrite);
        return;
    }

    // setup a start for child process
    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdError = hChildStdoutWrite;
    si.hStdOutput = hChildStdoutWrite;
    si.hStdInput = hChildStdinRead;
    si.dwFlags |= STARTF_USESTDHANDLES;

    const char* shell_path = "cmd.exe";
    char commandLine[MAX_PATH];
    sprintf_s(commandLine, MAX_PATH, "%s", shell_path);

    BOOL success = CreateProcessA(
        NULL,           // lpApplicationName
        commandLine,    // lpCommandLine
        NULL,           // lpProcessAttributes
        NULL,           // lpThreadAttributes
        TRUE,           // bInheritHandles (VERY IMPORTANT for pipe redirection)
        CREATE_NEW_CONSOLE | NORMAL_PRIORITY_CLASS,
        NULL,           // lpEnvironment
        NULL,           // lpCurrentDirectory
        &si,            // lpStartupInfo
        &piProcInfo     // lpProcessInformation
    );

    if (!success) {
        statusMessage = "Error: Failed to create child process. (GLE: " + std::to_string(GetLastError()) + ")";
        statusMessageTime = GetTickCount64();
        
        CloseHandle(hChildStdinRead); CloseHandle(hChildStdinWrite);
        CloseHandle(hChildStdoutRead); CloseHandle(hChildStdoutWrite);
        return;
    }

    CloseHandle(hChildStdinRead);
    CloseHandle(hChildStdinWrite);
    
    terminalActive = true;
    CreateThread(NULL, 0, TerminalOutputReaderThread, this, 0, NULL);
    
    statusMessage = "Terminal Started. Shell: " + std::string(shell_path);
    statusMessageTime = GetTickCount64();

    clearTerminalBuffer();
    terminalCursorX = 0;
    terminalCursorY = 0;
    terminalScrollOffset = 0;
    currentFgColor = defaultFgColor;
    currentBgColor = defaultBgColor;
    currentBold = false;
    ansiSGRParams.clear();
    asiEscapeBuffe.clear();

    resizeTerminal(screenCols, screenRows - 2);
}

void Editor::stopTerminal() {
    if (!terminalActive) return;
    terminalActive = false;

    if (piProcInfo.hProcess != NULL) {
        TerminateProcess(piProcInfo.hProcess, 0);
        CloseHandle(piProcInfo.hProcess);
        CloseHandle(piProcInfo.hThread);
        ZeroMemory(&piProcInfo, sizeof(piProcInfo));
    }

    if (hChildStdinWrite != NULL) { CloseHandle(hChildStdinWrite); hChildStdinWrite = NULL; }
    if (hChildStdoutRead != NULL) { CloseHandle(hChildStdoutRead); hChildStdoutRead = NULL; }

    statusMessage = "Terminal stopped.";
    statusMessageTime = GetTickCount64();
}

DWORD WINAPI TerminalOutputReaderThread(LPVOID lpParam) {
    Editor* editor = (Editor*)lpParam;
    char buffer[4096];
    DWORD bytesRead;

    const DWORD read_interval_ms = 10;

    while(editor->terminalActive) {
        if (ReadFile(editor->hChildStdoutRead, buffer, sizeof(buffer), &bytesRead, NULL)) {
            if (bytesRead > 0) {
                editor->processTerminalOutput(std::string(buffer, bytesRead));
            } 
        } else {
            DWORD error = GetLastError();
            if (error == ERROR_BROKEN_PIPE || error == ERROR_NO_DATA) {
                if (editor->terminalActive) {
                    editor->stopTerminal();
                }
                break;
            } else {
                std::cerr << "Terminal ReadFile Error: " << error << std::endl;
            }
        }
        Sleep(read_interval_ms);
    }
    return 0;
}

void Editor::readTerminalOutput() {
    // The reading is handled by the background thread.
    // This function can be a no-op or merely exist to fit the refreshScreen dispatch.
    // The thread calls processTerminalOutput, which will modify the buffer
    // and ideally trigger a redraw (which refreshScreen handles on next loop).
}

void Editor::writeTerminalInput(const std::string& input) {
    if (hChildStdinWrite == NULL) return;
    DWORD bytesWriten;

    WriteFile(hChildStdinWrite, input.c_str(), input.length(), &bytesWriten, NULL);
}

void Editor::processTerminalOutput(const std::string& data) {
    // Basic ANSI parser state machine
    for (char c : data) {
        if (!asiEscapeBuffe.empty()) {
            asiEscapeBuffe += c;
            if (c >= '@' && c <= '~') { // End of a CSI (Control Sequence Introducer) sequence
                if (asiEscapeBuffe.length() > 2 && asiEscapeBuffe[1] == '[') {
                    std::string params_str = asiEscapeBuffe.substr(2, asiEscapeBuffe.length() - 3);
                    ansiSGRParams.clear();

                    if (params_str.empty()) {
                        ansiSGRParams.push_back(0);
                    } else {
                        std::istringstream iss(params_str);
                        std::string segment;
                        while(std::getline(iss, segment, ';')) {
                            try {
                                ansiSGRParams.push_back(std::stoi(segment));
                            } catch (...) { /* Invalid param, ignore */ }
                        }
                    }

                    char final_char = asiEscapeBuffe.back();
                    if (final_char == 'm') {
                        applyAnsiSGR(ansiSGRParams);
                    } else if (final_char == 'H' || final_char == 'f') { // CUP or HVP
                        int row = ansiSGRParams.empty() ? 1 : ansiSGRParams[0];
                        int col = (ansiSGRParams.size() < 2) ? 1 : ansiSGRParams[1];
                        applyAnsiCUP(row, col);
                    } else if (final_char == 'J') { // ED (Erase Display)
                        int param = ansiSGRParams.empty() ? 0 : ansiSGRParams[0];
                        applyAnsiED(param);
                    } else if (final_char == 'K') { // EL (Erase Line)
                        int param = ansiSGRParams.empty() ? 0 : ansiSGRParams[0];
                        applyAnsiEL(param);
                    } else if (final_char == 'A') { // CUU - Cursor Up
                         int num = ansiSGRParams.empty() ? 1 : ansiSGRParams[0];
                         terminalCursorY = std::max(0, terminalCursorY - num);
                    } else if (final_char == 'B') { // CUD - Cursor Down
                         int num = ansiSGRParams.empty() ? 1 : ansiSGRParams[0];
                         terminalCursorY = std::min(terminalHeight - 1, terminalCursorY + num);
                    } else if (final_char == 'C') { // CUF - Cursor Forward
                         int num = ansiSGRParams.empty() ? 1 : ansiSGRParams[0];
                         terminalCursorX = std::min(terminalWidth - 1, terminalCursorX + num);
                    } else if (final_char == 'D') { // CUB - Cursor Backward
                         int num = ansiSGRParams.empty() ? 1 : ansiSGRParams[0];
                         terminalCursorX = std::max(0, terminalCursorX - num);
                    }
                    // Add more if needed (e.g., S/T for scroll, L/M for insert/delete line)
                }
                asiEscapeBuffe.clear();
            }
        } else if (c == 0x1B) { // ESC
            asiEscapeBuffe += c;
        } else if (c == '\r') {
            terminalCursorX = 0;
        } else if (c == '\n') {
            terminalCursorY++;
            terminalCursorX = 0; // Usually moves to column 0 on newline
            if (terminalCursorY >= terminalHeight) {
                // Scroll up the buffer by shifting lines
                for (int r = 0; r < terminalHeight - 1; ++r) {
                    terminalBuffer[r] = terminalBuffer[r+1];
                }
                // Clear the last line
                terminalBuffer[terminalHeight-1].assign(terminalWidth, { ' ', currentFgColor, currentBgColor, currentBold });
                terminalCursorY = terminalHeight - 1; // Stay on the last line
                // terminalScrollOffset is not used for internal buffer scroll here
            }
        } else if (c == '\b') { // Backspace
            if (terminalCursorX > 0) {
                terminalCursorX--;
                // Optionally: clear the character that was backspaced over
                if (terminalCursorY < terminalHeight && terminalCursorX < terminalWidth) {
                    terminalBuffer[terminalCursorY][terminalCursorX] = { ' ', currentFgColor, currentBgColor, currentBold };
                }
            }
        } else if (c == '\t') { // Tab
            terminalCursorX += (KILO_TAB_STOP - (terminalCursorX % KILO_TAB_STOP));
            if (terminalCursorX >= terminalWidth) terminalCursorX = terminalWidth - 1; // Clamp
            // Tab character itself is not stored in buffer if expanding
            // To store tab characters literally in buffer, then expand on drawing (like editor mode)
            // would require different handling. For now, it moves cursor.
        } else {
            // Regular character
            if (terminalCursorY < terminalHeight && terminalCursorX < terminalWidth) {
                terminalBuffer[terminalCursorY][terminalCursorX] = { c, currentFgColor, currentBgColor, currentBold };
            }
            terminalCursorX++;
            if (terminalCursorX >= terminalWidth) { // Auto-wrap
                terminalCursorX = 0;
                terminalCursorY++;
                if (terminalCursorY >= terminalHeight) { // Scroll if needed
                    for (int r = 0; r < terminalHeight - 1; ++r) {
                        terminalBuffer[r] = terminalBuffer[r+1];
                    }
                    terminalBuffer[terminalHeight-1].assign(terminalWidth, { ' ', currentFgColor, currentBgColor, currentBold });
                    terminalCursorY = terminalHeight - 1;
                }
            }
        }
    }

    for (auto& s : prevDrawnLines)
      s.assign(screenCols, ' ');
    prevStatusMessage = ""; 
    prevMessageBarMessage = "";
}

void Editor::resizeTerminal(int width, int height) {
    terminalWidth = width;
    terminalHeight = height;
    terminalBuffer.resize(terminalHeight);
    for (int r = 0; r < terminalHeight; ++r) {
        terminalBuffer[r].resize(terminalWidth, { ' ', defaultFgColor, defaultBgColor, false });
    }
    // Reset cursor and scroll after resize
    terminalCursorX = 0;
    terminalCursorY = 0;
    terminalScrollOffset = 0; // Not actively used for internal buffer scrolling in this model
    currentFgColor = defaultFgColor;
    currentBgColor = defaultBgColor;
    currentBold = false;
    ansiSGRParams.clear();
    asiEscapeBuffe.clear();
    clearTerminalBuffer(); // Clear content
    
    for (auto& s : prevDrawnLines)
      s.assign(screenCols, ' ');  // Invalidate editor content cache
    prevStatusMessage = "";       // Invalidate status message cache
    prevMessageBarMessage = "";   // Invalidate message bar cache
}

void Editor::drawTerminalScreen() {
    for (int y = 0; y < terminalHeight; ++y) {
        setCursorPosition(0, y); // Start drawing at screen (0,y)
        for (int x = 0; x < terminalWidth; ++x) {
            TerminalChar tc = terminalBuffer[y][x];
            WORD attributes = tc.fgColor | tc.bgColor;
            if (tc.bold) attributes |= FOREGROUND_INTENSITY; // Apply intensity for bold

            // To avoid flashing/flicker from character-by-character writes,
            // we should group characters with the same attribute.
            // This is a simple per-char write, which can flicker.
            setTextColor(attributes);
            writeStringAt(x, y, std::string(1, tc.c));
        }
        resetTextColor();
    }
}

void Editor::clearTerminalBuffer() {
    terminalBuffer.assign(terminalHeight, std::vector<TerminalChar>(terminalWidth, { ' ', defaultFgColor, defaultBgColor, false }));
}

void Editor::applyAnsiSGR(const std::vector<int>& params) {
  if (params.empty()) {  // Default: reset all attributes
    currentFgColor = defaultFgColor;
    currentBgColor = defaultBgColor;
    currentBold = false;
  }
  for (int param : params) {
    switch (param) {
      case 0:  // Reset all attributes
        currentFgColor = defaultFgColor;
        currentBgColor = defaultBgColor;
        currentBold = false;
        break;
      case 1:  // Bold/Increased Intensity
        currentBold = true;
        break;
      case 22:  // Not Bold/Normal Intensity
        currentBold = false;
        break;
      case 30:
        currentFgColor = BLACK;
        break;
    case 31:
        currentFgColor = RED;
        break;
    case 32:
        currentFgColor = GREEN;
        break;
    case 33:
        currentFgColor = YELLOW;
        break;
    case 34:
        currentFgColor = BLUE;
        break;
    case 35:
        currentFgColor = MAGENTA;
        break;
      case 36:
        currentFgColor = CYAN;
        break;
      case 37:
        currentFgColor = WHITE;
        break;
      case 39:
        currentFgColor = defaultFgColor;
        break;  // Default foreground color
      case 40:
        currentBgColor = BLACK;
        break;
      case 41:
        currentBgColor = BG_RED;
        break;
      case 42:
        currentBgColor = BG_GREEN;
        break;
      case 43:
        currentBgColor = BG_YELLOW;
        break;
      case 44:
        currentBgColor = BG_BLUE;
        break;
      case 45:
        currentBgColor = BG_MAGENTA;
        break;
      case 46:
        currentBgColor = BG_CYAN;
        break;
      case 47:
        currentBgColor = BG_WHITE;
        break;
      case 49:
        currentBgColor = defaultBgColor;
        break;  // Default background color
      default:
        break;
    }
  }
}

void Editor::applyAnsiCUP(int row, int col) {
  // ANSI rows/cols are 1-based. Convert to 0-based.
  terminalCursorY = std::max(0, row - 1);
  terminalCursorX = std::max(0, col - 1);

  // Clamp to screen bounds
  if (terminalCursorY >= terminalHeight)
    terminalCursorY = terminalHeight - 1;
  if (terminalCursorX >= terminalWidth)
    terminalCursorX = terminalWidth - 1;
}

void Editor::applyAnsiED(int param) {
  if (param == 0) {  // Erase from cursor to end of screen
    // Clear current line from cursor
    for (int x = terminalCursorX; x < terminalWidth; ++x) {
      terminalBuffer[terminalCursorY][x] = {' ', currentFgColor, currentBgColor,
                                            currentBold};
    }
    // Clear subsequent lines
    for (int y = terminalCursorY + 1; y < terminalHeight; ++y) {
      terminalBuffer[y].assign(
          terminalWidth, {' ', currentFgColor, currentBgColor, currentBold});
    }
  } else if (param == 1) {  // Erase from cursor to beginning of screen
    // Clear current line from beginning to cursor
    for (int x = 0; x <= terminalCursorX; ++x) {
      terminalBuffer[terminalCursorY][x] = {' ', currentFgColor, currentBgColor,
                                            currentBold};
    }
    // Clear preceding lines
    for (int y = 0; y < terminalCursorY; ++y) {
      terminalBuffer[y].assign(
          terminalWidth, {' ', currentFgColor, currentBgColor, currentBold});
    }
  } else if (param == 2) {  // Erase entire screen
    clearTerminalBuffer();
  }
}

void Editor::applyAnsiEL(int param) {
  if (param == 0) {  // Erase from cursor to end of line
    for (int x = terminalCursorX; x < terminalWidth; ++x) {
      terminalBuffer[terminalCursorY][x] = {' ', currentFgColor, currentBgColor,
                                            currentBold};
    }
  } else if (param == 1) {  // Erase from cursor to beginning of line
    for (int x = 0; x <= terminalCursorX; ++x) {
      terminalBuffer[terminalCursorY][x] = {' ', currentFgColor, currentBgColor,
                                            currentBold};
    }
  } else if (param == 2) {  // Erase entire line
    terminalBuffer[terminalCursorY].assign(
        terminalWidth, {' ', currentFgColor, currentBgColor, currentBold});
  }
}

void Editor::refreshScreen() {
    setCursorVisibility(false);

    updateScreenSize();

    if (mode == EDIT_MODE || mode == PROMPT_MODE) {
        scroll();   
        drawScreenContent();
    } else if (mode == FILE_EXPLORER_MODE) {
        drawFileExplorer();
    } else if (mode == TERMINAL_MODE) {
        readTerminalOutput();
        drawTerminalScreen();
    }

    drawStatusBar();
    drawMessageBar();


    int finalCursorX = 0;
    int finalCursorY = 0;
    if (mode == EDIT_MODE) {
        int renderedCursorInLine = cxToRx(cursorY, cursorX);
        int cursorXRelativeToTextArea = renderedCursorInLine - colOffset;
        finalCursorX = lineNumberWidth + cursorXRelativeToTextArea;
        finalCursorY = cursorY - rowOffset;
        finalCursorX = std::max(0, std::min(finalCursorX, screenCols - 1));
        finalCursorY = std::max(0, std::min(finalCursorY, screenRows - 3));
    } else if (mode == PROMPT_MODE) {
        finalCursorX = promptMessage.length() + searchQuery.length();
        finalCursorY = screenRows - 1; // Last row for prompt
        finalCursorX = std::min(finalCursorX, screenCols - 1);
    } else if (mode == TERMINAL_MODE) {
        finalCursorX = terminalCursorX;
        finalCursorY = terminalCursorY;
        finalCursorX = std::max(0, std::min(finalCursorX, screenCols - 1));
        finalCursorY = std::max(0, std::min(finalCursorY, screenRows - 3));
    }
    else { 
        int explorer_content_start_row = 2; // PATH line (row 0) + 1 blank line (row 1) = content starts at row 2
        finalCursorX = 0; // Always column 0 for selected item's visual cue
        finalCursorY = explorer_content_start_row + (selectedFileIndex - fileExplorerScrollOffset);

        finalCursorY = std::max(explorer_content_start_row, std::min(finalCursorY, screenRows - 3));
        finalCursorX = std::min(finalCursorX, screenCols - 1); // Clamp X (always 0, should be fine)
    }

    setCursorPosition(finalCursorX, finalCursorY);
    setCursorVisibility(true);
}

void Editor::clearScreen() {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole == INVALID_HANDLE_VALUE) {
        std::cerr << "Error: Invalid console handle in clearScreen." << std::endl;
        return;
    }

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(hConsole, &csbi)) {
        std::cerr << "Error: GetConsoleScreenBufferInfo failed in clearScreen. GLE: " << GetLastError() << std::endl;
        return;
    }

    DWORD cellsCount = csbi.dwSize.X * csbi.dwSize.Y;
    COORD homeCoords = { 0, 0 };
    DWORD charsWritten;

    // Fill the entire screen buffer with spaces
    if (!FillConsoleOutputCharacterA(hConsole,      // Handle to console screen buffer
                                     (TCHAR)' ',    // Character to write to the buffer
                                     cellsCount,    // Number of cells to write
                                     homeCoords,    // Coordinate of the first cell
                                     &charsWritten)) // Number of characters written
    {
        std::cerr << "Error: FillConsoleOutputCharacterA failed in clearScreen. GLE: " << GetLastError() << std::endl;
        return;
    }

    // Fill the entire screen buffer with the current default attributes
    // This ensures consistent background/foreground colors after clearing.
    // Using defaultFgColor and defaultBgColor stored in Editor for consistency.
    if (!FillConsoleOutputAttribute(hConsole,        // Handle to console screen buffer
                                    defaultFgColor | defaultBgColor, // Attributes to set
                                    cellsCount,      // Number of cells to set attributes
                                    homeCoords,      // Coordinate of the first cell
                                    &charsWritten))  // Number of cells modified
    {
        std::cerr << "Error: FillConsoleOutputAttribute failed in clearScreen. GLE: " << GetLastError() << std::endl;
        return;
    }

    // Move the cursor to the home position (top-left)
    SetConsoleCursorPosition(hConsole, homeCoords);
}

void Editor::force_full_redraw_internal() {
    for (auto& s : prevDrawnLines) {
        s.assign(screenCols, ' '); // Fill with spaces to force a diff
    }

    // Invalidate cached status and message bar content
    prevStatusMessage = "";
    prevMessageBarMessage = "";
}