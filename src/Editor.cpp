#include "editor.h"
#include <sstream>
#include <algorithm>
#include "win_console_utils.h"
#include <iostream>
#include <map>
#include "lua_api.h"
#include <Shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")

DWORD WINAPI TerminalOutputReaderThread(LPVOID lpParam);
struct FontEnumData {
    std::vector<ConsoleFontInfo> fonts;
};

BOOL CALLBACK EnumFontFamExProc(
    _In_      ENUMLOGFONTEX *lpelfe,
    _In_      NEWTEXTMETRICEX *lpntme,
    _In_      DWORD FontType,
    _In_      LPARAM lParam
);

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

    if (!lua_isstring(L, 1)) {
        return luaL_error(L, "Argument #1 (message) for set_status_message must be a string.");
    }
    std::string message = lua_tostring(L, 1);

    ULONGLONG duration_ms = 5000;
    if (lua_isinteger(L, 2)) {
        duration_ms = lua_tointeger(L, 2);
    }

    editor->statusMessage = message;
    editor->statusMessageTime = GetTickCount64() + duration_ms;
    return 0;
}

int lua_get_current_time_ms(lua_State* L) {
    lua_pushinteger(L, GetTickCount64());
    return 1;
}

int lua_set_tab_stop_width(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    if (!lua_isinteger(L, 1)) return luaL_error(L, "Argument #1 (width) must be an integer.");
    int width = lua_tointeger(L, 1);
    if (width > 0 && width <= 16) {
        editor->kiloTabStop = width;
        editor->calculateLineNumberWidth();
        editor->force_full_redraw_internal();
        editor->statusMessage = "Tab stop width set to " + std::to_string(width);
        editor->statusMessageTime = GetTickCount64() + 2000;
    } else {
        editor->show_error("Invalid tab stop width. Must be between 1 and 16.", 3000); // CALL MEMBER FUNCTION
        return luaL_error(L, "Invalid tab stop width. Must be between 1 and 16."); // Still return Lua error
    }
    return 0;
}

int lua_set_default_line_ending(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    if (!lua_isstring(L, 1)) return luaL_error(L, "Argument #1 (type) must be a string ('CRLF' or 'LF').");
    std::string type = lua_tostring(L, 1);
    if (type == "CRLF") {
        editor->currentLineEnding = Editor::LE_CRLF;
        editor->statusMessage = "Default line ending set to CRLF";
    } else if (type == "LF") {
        editor->currentLineEnding = Editor::LE_LF;
        editor->statusMessage = "Default line ending set to LF";
    } else {
        editor->show_error("Invalid line ending type. Must be 'CRLF' or 'LF'.", 3000);
        return luaL_error(L, "Invalid line ending type. Must be 'CRLF' or 'LF'."); 
    }
    editor->statusMessageTime = GetTickCount64() + 2000;
    return 0;
}

int lua_refresh_screen(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    editor->refreshScreen();
    return 0;
}

int lua_insert_text(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");

    if (!lua_isstring(L, 1)) {
        return luaL_error(L, "Argument #1 (text) for insert_text must be a string.");
    }
    std::string text = lua_tostring(L, 1);

    for (char c : text) {
        if (c == '\n') {
            editor->insertNewline();
        } else {
            editor->insertChar(c);
        }
    }
    editor->dirty = true;
    editor->calculateLineNumberWidth();
    editor->triggerEvent("on_buffer_changed");
    editor->triggerEvent("on_cursor_moved");
    return 0;
}

int lua_get_current_line_text(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");

    std::string line_text = "";
    if (editor->cursorY >= 0 && editor->cursorY < editor->lines.size()) {
        line_text = editor->lines[editor->cursorY];
    }
    lua_pushstring(L, line_text.c_str());
    return 1;
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
    editor->force_full_redraw_internal();
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

    int line_num = lua_tointeger(L, 1) - 1;
    if (line_num >= 0 && line_num < editor->lines.size()) {
        lua_pushstring(L, editor->lines[line_num].c_str());
    } else {
        lua_pushstring(L, "");
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
        editor->force_full_redraw_internal();
        editor->triggerEvent("on_buffer_changed");
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

    if (line_num >= 0 && line_num <= editor->lines.size()) {
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

    if (editor->lines.empty()) return 0;
    if (line_num >= 0 && line_num < editor->lines.size()) {
        editor->lines.erase(editor->lines.begin() + line_num);
        if (editor->lines.empty()) {
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

    lua_newtable(L);
    for (int i = 0; i < editor->lines.size(); ++i) {
        lua_pushstring(L, editor->lines[i].c_str());
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

int lua_set_buffer_content(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    if (!lua_istable(L, 1)) return luaL_error(L, "Argument #1 (content) must be a table of strings.");

    editor->lines.clear();
    int table_len = luaL_len(L, 1);
    for (int i = 1; i <= table_len; ++i) {
        lua_rawgeti(L, 1, i);
        if (lua_isstring(L, -1)) {
            editor->lines.push_back(lua_tostring(L, -1));
        } else {
            editor->lines.push_back("");
        }
        lua_pop(L, 1);
    }
    if (editor->lines.empty()) editor->lines.push_back("");

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
    lua_pushinteger(L, KILO_TAB_STOP);
    return 1;
}

int lua_set_cursor_position(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    if (!lua_isinteger(L, 1) || !lua_isinteger(L, 2)) return luaL_error(L, "Arguments (x, y) must be integers.");

    int x = lua_tointeger(L, 1) - 1;
    int y = lua_tointeger(L, 2) - 1;

    if (y < 0) y = 0;
    if (y >= editor->lines.size()) y = editor->lines.size() - 1;
    if (editor->lines.empty()) { 
        y = 0; x = 0;
    } else {
        if (x < 0) x = 0;
        if (x > editor->lines[y].length()) x = editor->lines[y].length();
    }

    editor->cursorX = x;
    editor->cursorY = y;
    editor->scroll();
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
    editor->scroll();
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
    editor->populateDirectoryEntries(path);
    editor->currentDirPath = path;
    return 0;
}

int lua_list_directory(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    std::string path = lua_tostring(L, 1);

    std::string targetPath = path.empty() ? editor->currentDirPath : path;

    std::vector<DirEntry> entries;
    WIN32_FIND_DATAA findFileData;
    HANDLE hFind = FindFirstFileA((targetPath + "\\*").c_str(), &findFileData);

    if (hFind == INVALID_HANDLE_VALUE) {
        lua_pushnil(L);
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

    std::sort(entries.begin(), entries.end(), [](const DirEntry& a, const DirEntry& b) {
        if (a.isDirectory && !b.isDirectory) return true;
        if (!a.isDirectory && b.isDirectory) return false;
        return a.name < b.name;
    });

    lua_newtable(L);
    for (int i = 0; i < entries.size(); ++i) {
        lua_newtable(L);
        lua_pushstring(L, entries[i].name.c_str());
        lua_setfield(L, -2, "name");
        lua_pushboolean(L, entries[i].isDirectory);
        lua_setfield(L, -2, "is_directory");
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
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

int lua_prompt_user(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    if (!lua_isstring(L, 1)) return luaL_error(L, "Argument #1 (prompt_message) must be a string.");

    std::string prompt_msg = lua_tostring(L, 1);
    std::string default_val = "";
    if (lua_isstring(L, 2)) {
        default_val = lua_tostring(L, 2);
    }

    editor->mode = PROMPT_MODE;
    editor->promptMessage = prompt_msg;
    editor->searchQuery = default_val;
    editor->statusMessage = editor->promptMessage + editor->searchQuery;
    editor->statusMessageTime = GetTickCount64();
    editor->force_full_redraw_internal();

    lua_pushboolean(L, true);
    return 1;
}

int lua_show_message(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    if (!lua_isstring(L, 1)) return luaL_error(L, "Argument #1 (message) must be a string.");

    std::string message = lua_tostring(L, 1);
    ULONGLONG duration_ms = 5000;
    if (lua_isinteger(L, 2)) {
        duration_ms = lua_tointeger(L, 2);
    }

    editor->statusMessage = message;
    editor->statusMessageTime = GetTickCount64() + duration_ms;
    return 0;
}

int lua_show_error(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    if (!lua_isstring(L, 1)) return luaL_error(L, "Argument #1 (error_message) must be a string.");

    std::string message = "Error: " + std::string(lua_tostring(L, 1));
    ULONGLONG duration_ms = 8000;
    if (lua_isinteger(L, 2)) {
        duration_ms = lua_tointeger(L, 2);
    }

    editor->statusMessage = message;
    editor->statusMessageTime = GetTickCount64() + duration_ms;
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

    int result = system(command.c_str());
    lua_pushinteger(L, result);
    return 1;
}

int lua_register_event_handler(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    if (!lua_isstring(L, 1)) return luaL_error(L, "Argument #1 (event_name) must be a string.");
    if (!lua_isfunction(L, 2)) return luaL_error(L, "Argument #2 (handler_function) must be a function.");

    std::string event_name = lua_tostring(L, 1);
    lua_pushvalue(L, 2);
    int funcRef = luaL_ref(L, LUA_REGISTRYINDEX);

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
        luaL_unref(L, LUA_REGISTRYINDEX, funcRef);
        return luaL_error(L, "Unknown event name: %s", event_name.c_str());
    }
    return 0;
}

int lua_is_ctrl_pressed(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    lua_pushboolean(L, editor->ctrl_pressed);
    return 1;
}

static std::map<std::string, std::string> plugin_data_storage; // Use map for simplicity, convert to file IO later

int lua_save_plugin_data(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    if (!lua_isstring(L, 1)) return luaL_error(L, "Argument #1 (key) must be a string.");
    if (!lua_isstring(L, 2)) return luaL_error(L, "Argument #2 (value) must be a string.");

    std::string key = lua_tostring(L, 1);
    std::string value = lua_tostring(L, 2);

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
        lua_pushstring(L, plugin_data_storage[key].c_str());
        return 1;
    } else {
        lua_pushnil(L);
        return 1;
    }
}

int lua_set_console_font(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");

    if (!lua_isstring(L, 1)) return luaL_error(L, "Argument #1 (font_name) must be a string.");
    if (!lua_isinteger(L, 2)) return luaL_error(L, "Argument #2 (font_size_x) must be an integer.");
    if (!lua_isinteger(L, 3)) return luaL_error(L, "Argument #3 (font_size_y) must be an integer.");

    ConsoleFontInfo fontInfo;
    fontInfo.name = lua_tostring(L, 1);
    fontInfo.fontSizeX = (short)lua_tointeger(L, 2);
    fontInfo.fontSizeY = (short)lua_tointeger(L, 3);

    bool success = editor->setConsoleFont(fontInfo);
    lua_pushboolean(L, success);
    return 1;
}

int lua_get_current_console_font(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");

    ConsoleFontInfo fontInfo = editor->getCurrentConsoleFont();
    lua_pushstring(L, fontInfo.name.c_str());
    lua_pushinteger(L, fontInfo.fontSizeX);
    lua_pushinteger(L, fontInfo.fontSizeY);
    return 3;
}

BOOL CALLBACK EnumFontFamExProc(
  _In_      ENUMLOGFONTEX *lpelfe, // This is actually ENUMLOGFONTEXW in wide character builds
  _In_      NEWTEXTMETRICEX *lpntme,
  _In_      DWORD FontType,
  _In_      LPARAM lParam
) {
    FontEnumData* data = reinterpret_cast<FontEnumData*>(lParam);

    if ((FontType & TRUETYPE_FONTTYPE) && (lpelfe->elfLogFont.lfPitchAndFamily & FIXED_PITCH)) {
        if (lpelfe->elfLogFont.lfFaceName[0] != L'@' && lpelfe->elfLogFont.lfFaceName[0] != L'\0') {
            char buffer[LF_FACESIZE];

            WideCharToMultiByte(CP_ACP, 0, lpelfe->elfLogFont.lfFaceName, -1, buffer, LF_FACESIZE, NULL, NULL);
            data->fonts.push_back({std::string(buffer), 0, 0}); // Size fields are placeholders
        }
    }
    return TRUE; // Continue enumeration
}

int lua_get_available_console_fonts(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");

    std::vector<ConsoleFontInfo> fonts = editor->getAvailableConsoleFonts();

    lua_newtable(L);
    for (int i = 0; i < fonts.size(); ++i) {
        lua_pushinteger(L, i + 1);
        lua_newtable(L);
        
        lua_pushstring(L, fonts[i].name.c_str());
        lua_setfield(L, -2, "name");
        lua_pushinteger(L, fonts[i].fontSizeX);
        lua_setfield(L, -2, "size_x");
        lua_pushinteger(L, fonts[i].fontSizeY);
        lua_setfield(L, -2, "size_y");

        lua_settable(L, -3);
    }
    return 1;
}

// Update the `editor_lib` array to include all new functions
static const luaL_Reg editor_lib[] = {
    {"set_status_message", lua_set_status_message},
    {"get_current_time_ms", lua_get_current_time_ms},
    {"refresh_screen", lua_refresh_screen},
    {"force_full_redraw", lua_force_full_redraw},
    {"insert_text", lua_insert_text},
    {"get_current_line_text", lua_get_current_line_text},
    {"get_line_count", lua_get_line_count},
    {"get_cursor_x", lua_get_cursor_x},
    {"get_cursor_y", lua_get_cursor_y},
    {"get_current_filename", lua_get_current_filename},

    {"get_line", lua_get_line},
    {"set_line", lua_set_line},
    {"insert_line", lua_insert_line},
    {"delete_line", lua_delete_line},
    {"get_buffer_content", lua_get_buffer_content},
    {"set_buffer_content", lua_set_buffer_content},
    {"get_char_at", lua_get_char_at},
    {"get_tab_stop_width", lua_get_tab_stop_width},
    {"set_cursor_position", lua_set_cursor_position},
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
    {"prompt", lua_prompt_user},
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

    // Font Controls
    {"set_console_font", lua_set_console_font},
    {"get_current_console_font", lua_get_current_console_font},
    {"get_available_console_fonts", lua_get_available_console_fonts},

    {NULL, NULL}
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
    currentLineEnding(LE_CRLF),
    hChildStdinRead(NULL), hChildStdinWrite(NULL), hChildStdoutRead(NULL), hChildStdoutWrite(NULL),
    hConsoleInput(INVALID_HANDLE_VALUE), originalConsoleMode(0),
    currentFgColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE), // Default white
    currentBgColor(0),
    currentBold(false),
    defaultFgColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE),
    defaultBgColor(0)
{
    lines.push_back("");
    updateScreenSize();
    
    currentFont = getCurrentConsoleFont();

    hConsoleInput = GetStdHandle(STD_INPUT_HANDLE);
    if (hConsoleInput != INVALID_HANDLE_VALUE) {
        if (!GetConsoleMode(hConsoleInput, &originalConsoleMode)) {
            std::cerr << "Error getting console mode: " << GetLastError() << std::endl;
        }
        // Set the desired raw input mode for the editor
        DWORD newMode = originalConsoleMode;
        newMode &= ~ENABLE_ECHO_INPUT;      // Disable echoing input
        newMode &= ~ENABLE_LINE_INPUT;      // Disable line buffering (return on any key)
        newMode &= ~ENABLE_PROCESSED_INPUT; // Disable Ctrl+C, Ctrl+Break system processing
        // newMode |= ENABLE_VIRTUAL_TERMINAL_INPUT; // Enable if you want to explicitly parse VT100 input sequences
                                                    // (like arrow keys as ESC[A, ESC[B etc. directly in your input loop)
                                                    // Your current approach with ReadConsoleInput should work fine without this.
        if (!SetConsoleMode(hConsoleInput, newMode)) {
            std::cerr << "Error setting console mode: " << GetLastError() << std::endl;
        }
    }

    prevDrawnLines.resize(screenRows - 2);
    for (auto& s : prevDrawnLines) s.assign(screenCols, ' ');

    char buffer[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, buffer);
    currentDirPath = buffer;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        defaultFgColor = csbi.wAttributes & (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        defaultBgColor = csbi.wAttributes & (BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE | BACKGROUND_INTENSITY);
        currentFgColor = defaultFgColor;
        currentBgColor = defaultBgColor;
    } else {
        defaultFgColor = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE; // Default white
        defaultBgColor = 0; // Default black
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

    if (hConsoleInput != INVALID_HANDLE_VALUE && originalConsoleMode != 0) {
        if (!SetConsoleMode(hConsoleInput, originalConsoleMode)) {
            std::cerr << "Error restoring console mode: " << GetLastError() << std::endl;
        }
    }

    stopTerminal();
}

void Editor::triggerEvent(const std::string& eventName, int param) {
    std::vector<LuaCallback>* callbacks = nullptr;
    if (eventName == "on_key_press") callbacks = &onKeyPressCallbacks;
    else if (eventName == "on_mode_changed") callbacks = &onModeChangedCallbacks;

    if (callbacks) {
        for (const auto& callback : *callbacks) {
            lua_rawgeti(callback.L_state, LUA_REGISTRYINDEX, callback.funcRef);
            lua_pushinteger(callback.L_state, param);
            int status = lua_pcall(callback.L_state, 1, 0, 0);
            if (status != LUA_OK) {
                std::string error_msg = lua_tostring(callback.L_state, -1);
                lua_pop(callback.L_state, 1);
                show_error("Error in Lua event '" + eventName + "': " + error_msg, 10000);
                std::cerr << "Error in Lua event '" << eventName << "': " << error_msg << std::endl;
            }
        }
    }
}

void Editor::triggerEvent(const std::string& eventName, const std::string& param) {
    std::vector<LuaCallback>* callbacks = nullptr;
    if (eventName == "on_file_opened") callbacks = &onFileOpenedCallbacks;
    else if (eventName == "on_file_saved") callbacks = &onFileSavedCallbacks;

    if (callbacks) {
        for (const auto& callback : *callbacks) {
            lua_rawgeti(callback.L_state, LUA_REGISTRYINDEX, callback.funcRef);
            lua_pushstring(callback.L_state, param.c_str());
            int status = lua_pcall(callback.L_state, 1, 0, 0);
            if (status != LUA_OK) {
                std::string error_msg = lua_tostring(callback.L_state, -1);
                lua_pop(callback.L_state, 1);
                show_error("Error in Lua event '" + eventName + "': " + error_msg, 10000);
                std::cerr << "Error in Lua event '" << eventName << "': " << error_msg << std::endl;
            }
        }
    }
}

void Editor::initializeLua() {
    L = luaL_newstate();
    if (!L) {
        std::cerr << "Error: Failed to create Lua state." << std::endl;
        exit(1);
    }

    int status = luaL_dofile(L, "config.lua");
    if (status != LUA_OK) {
        std::cerr << "Warning: Could not load config.lua: " << lua_tostring(L, -1) << std::endl;
        lua_pop(L, 1);
    }

    luaL_openlibs(L);

    exposeEditorToLua();

    statusMessage = "Lua interpreter initialized.";
    statusMessageTime = GetTickCount64();
}

void Editor::finalizeLua() {
    if (L) {
        lua_close(L);
        L = nullptr;
    }
    statusMessage = "Lua interpreter finalized.";
    statusMessageTime = GetTickCount64();
}

void Editor::exposeEditorToLua() {
    lua_newtable(L);
    lua_pushlightuserdata(L, this);
    luaL_setfuncs(L, editor_lib, 1);
    lua_setglobal(L, "editor_api");

    lua_getglobal(L, "package");
    lua_getfield(L, -1, "path");
    std::string current_path = lua_tostring(L, -1);
    current_path += ";./plugins/?.lua";
    lua_pop(L, 1);
    lua_pushstring(L, current_path.c_str());
    lua_setfield(L, -2, "path");
    lua_pop(L, 1);

    if (lua_status(L) != LUA_OK) {
        std::cerr << "Error exposing Editor to Lua: " << lua_tostring(L, -1) << std::endl;
        lua_pop(L, 1);
    }
}

void Editor::loadLuaPlugins(const std::string& pluginDir) {
    if (!L) {
        show_error("Lua interpreter not initialized, cannot load plugins.");
        return;
    }
    statusMessage = "Loading Lua plugins from '" + pluginDir + "'...";
    statusMessageTime = GetTickCount64();

    CreateDirectoryA(pluginDir.c_str(), NULL);

    WIN32_FIND_DATAA findFileData;
    HANDLE hFind = FindFirstFileA((pluginDir + "\\*.lua").c_str(), &findFileData);
    if (hFind == INVALID_HANDLE_VALUE) {
        statusMessage = "No Lua plugins found in '" + pluginDir + "'"; // This is a status, not an error
        statusMessageTime = GetTickCount64() + 2000;
        return;
    }

    do {
        std::string luaFileName = findFileData.cFileName;
        if (luaFileName == "." || luaFileName == "..") continue;

        std::string luaFilePath = pluginDir + "\\" + luaFileName;

        int status = luaL_dofile(L, luaFilePath.c_str());
        if (status != LUA_OK) {
            std::string error_msg = lua_tostring(L, -1);
            lua_pop(L, 1);
            show_error("Error loading Lua plugin '" + luaFileName + "': " + error_msg, 10000); // CALL MEMBER FUNCTION
            std::cerr << "Lua Error: " << error_msg << std::endl;
        } else {
            statusMessage = "Loaded Lua plugin: " + luaFileName;
            statusMessageTime = GetTickCount64();

            lua_getglobal(L, "on_load");
            if (lua_isfunction(L, -1)) {
                status = lua_pcall(L, 0, 0, 0);
                if (status != LUA_OK) {
                    std::string error_msg = lua_tostring(L, -1);
                    lua_pop(L, 1);
                    show_error("Error in plugin 'on_load' for '" + luaFileName + "': " + error_msg, 10000); // CALL MEMBER FUNCTION
                    std::cerr << "Lua on_load Error: " << error_msg << std::endl;
                }
            } else {
                lua_pop(L, 1);
            }
        }
    } while (FindNextFileA(hFind, &findFileData) != 0);

    FindClose(hFind);
    statusMessage = "Finished loading Lua plugins.";
    statusMessageTime = GetTickCount64();
}

void Editor::show_error(const std::string& message, ULONGLONG duration_ms) {
    this->statusMessage = "Error: " + message;
    this->statusMessageTime = GetTickCount64();
}

bool Editor::executeLuaPluginCommand(const std::string& pluginName, const std::string& commandName) {
    if (!L) {
        show_error("Lua interpreter not initialized, cannot execute plugin command.");
        return false;
    }
    lua_getglobal(L, commandName.c_str());

    if (!lua_isfunction(L, -1)) {
        std::string type_name = lua_typename(L, lua_type(L, -1));
        show_error("Command '" + commandName + "' not found or not a function in Lua. Type was: " + type_name); // CALL MEMBER FUNCTION
        lua_pop(L, 1);
        return false;
    }

    int status = lua_pcall(L, 0, 0, 0);
    if (status != LUA_OK) {
        std::string error_msg = lua_tostring(L, -1);
        lua_pop(L, 1);
        show_error("Lua Plugin Error in '" + commandName + "': " + error_msg, 10000); // CALL MEMBER FUNCTION
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
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    int newScreenRows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    int newScreenCols = csbi.srWindow.Right - csbi.srWindow.Left + 1;

    if (newScreenRows != screenRows || newScreenCols != screenCols)
    {
        screenRows = newScreenRows;
        screenCols = newScreenCols;
        calculateLineNumberWidth();

        // prevDrawnLines resized to cover the content area (screenRows - 2 lines)
        prevDrawnLines.resize(screenRows - 2);
        for (auto& s : prevDrawnLines)
        {
            s.assign(screenCols, ' ');
        }
        // No clearScreen() here, refreshScreen's mode check will handle it.
        // But do force a redraw because screen size changed.
        force_full_redraw_internal();
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

    int effectiveScreenCols = screenCols - lineNumberWidth;

    for (int i = 0; i < screenRows - 2; ++i) {
        int fileRow = rowOffset + i;
        std::string fullLineContentToDraw = "";
        std::vector<WORD> fullLineAttributes(screenCols);

        std::string lineNumberStr;
        std::ostringstream ss_lineNumber;
        if (fileRow >= 0 && fileRow < lines.size()) {
            ss_lineNumber << std::setw(lineNumberWidth - 1) << (fileRow + 1) << " ";
        } else {
            ss_lineNumber << std::string(lineNumberWidth - 1, ' ') << "~";
        }
        lineNumberStr = ss_lineNumber.str();
        fullLineContentToDraw += lineNumberStr;

        for (int k = 0; k < lineNumberStr.length(); ++k) {
            fullLineAttributes[k] = defaultFgColor | defaultBgColor;
        }

        std::string renderedTextContent = "";
        if (fileRow >= 0 && fileRow < lines.size()) {
            std::string fullRenderedLine = getRenderedLine(fileRow);

            int startCharInRenderedLine = colOffset;
            int endCharInRenderedLine = colOffset + effectiveScreenCols;

            startCharInRenderedLine = std::max(0, startCharInRenderedLine);
            startCharInRenderedLine = std::min(startCharInRenderedLine, (int)fullRenderedLine.length());
            endCharInRenderedLine = std::max(0, endCharInRenderedLine);
            endCharInRenderedLine = std::min(endCharInRenderedLine, (int)fullRenderedLine.length());

            if (endCharInRenderedLine > startCharInRenderedLine) {
                renderedTextContent = fullRenderedLine.substr(startCharInRenderedLine, endCharInRenderedLine - startCharInRenderedLine);
            }
        }
        fullLineContentToDraw += renderedTextContent;

        int currentScreenCol = lineNumberStr.length();
        for (int k = 0; k < renderedTextContent.length(); ++k) {
            WORD current_attributes = defaultFgColor | defaultBgColor;
            bool isHighlight = false;

            if (!searchQuery.empty() && currentMatchIndex != -1 && fileRow == searchResults[currentMatchIndex].first) {
                int matchLogicalStart = searchResults[currentMatchIndex].second;
                int matchLogicalEnd = searchResults[currentMatchIndex].second + searchQuery.length();

                int matchRenderedStart = cxToRx(fileRow, matchLogicalStart);
                int matchRenderedEnd = cxToRx(fileRow, matchLogicalEnd);

                int charGlobalRenderedPos = (currentScreenCol + k) - lineNumberStr.length() + colOffset;

                if (charGlobalRenderedPos >= matchRenderedStart && charGlobalRenderedPos < matchRenderedEnd) {
                    isHighlight = true;
                }
            }

            if (isHighlight) {
                current_attributes = BG_YELLOW | BLACK;
            }
            fullLineAttributes[currentScreenCol + k] = current_attributes;
        }

        for (int k = fullLineContentToDraw.length(); k < screenCols; ++k) {
            fullLineContentToDraw += ' ';
            fullLineAttributes[k] = defaultFgColor | defaultBgColor;
        }

        if (i >= prevDrawnLines.size() || prevDrawnLines[i] != fullLineContentToDraw) {
            COORD writePos = { 0, (SHORT)i };
            DWORD charsWritten;

            if (!WriteConsoleOutputCharacterA(hConsole, fullLineContentToDraw.c_str(), screenCols, writePos, &charsWritten)) {
                std::cerr << "WriteConsoleOutputCharacterA failed: " << GetLastError() << std::endl;
            }

            if (!WriteConsoleOutputAttribute(hConsole, fullLineAttributes.data(), screenCols, writePos, &charsWritten)) {
                std::cerr << "WriteConsoleOutputAttribute failed: " << GetLastError() << std::endl;
            }

            if (i < prevDrawnLines.size()) {
                prevDrawnLines[i] = fullLineContentToDraw;
            }
        }
    }

    drawStatusBar();
    drawMessageBar();
}

void Editor::startSearch() {
    originalCursorX = cursorX;
    originalCursorY = cursorY;
    originalRowOffset = rowOffset;
    originalColOffset = colOffset;

    mode = PROMPT_MODE;
    promptMessage = "Search: ";
    searchQuery = "";
    searchResults.clear();
    currentMatchIndex = -1;
    statusMessage = "Enter search term. ESC to cancel, Enter to search.";
    statusMessageTime = GetTickCount64();

    force_full_redraw_internal(); // Invalidate cache for new mode's content
}

void Editor::performSearch() {
    searchResults.clear();
    currentMatchIndex = -1;

    if (searchQuery.empty()) {
        statusMessage = "Search cancelled or empty.";
        statusMessageTime = GetTickCount64();
        mode = EDIT_MODE;
        cursorX = originalCursorX;
        cursorY = originalCursorY;
        rowOffset = originalRowOffset;
        colOffset = originalColOffset;
        force_full_redraw_internal(); // Revert to previous state, full redraw needed
        return;
    }

    for (int r = 0; r < lines.size(); ++r) {
        size_t pos = lines[r].find(searchQuery, 0);
        while (pos != std::string::npos) {
            searchResults.push_back({r, (int)pos});
            pos = lines[r].find(searchQuery, pos + 1);
        }
    }

    if (searchResults.empty()) {
        statusMessage = "No matches found for '" + searchQuery + "'";
        statusMessageTime = GetTickCount64();
        mode = EDIT_MODE;
        cursorX = originalCursorX;
        cursorY = originalCursorY;
        rowOffset = originalRowOffset;
        colOffset = originalColOffset;
        force_full_redraw_internal(); // Revert to previous state, full redraw needed
        return;
    }

    currentMatchIndex = 0;
    statusMessage = "Found " + std::to_string(searchResults.size()) + " matches. (N)ext (P)rev";
    statusMessageTime = GetTickCount64();

    cursorY = searchResults[currentMatchIndex].first;
    cursorX = searchResults[currentMatchIndex].second;
    scroll();
    
    mode = EDIT_MODE; // Exit search prompt mode
    force_full_redraw_internal();
}

void Editor::findNext() {
    if (searchResults.empty()) return;

    currentMatchIndex = (currentMatchIndex + 1) % searchResults.size();

    cursorY = searchResults[currentMatchIndex].first;
    cursorX = searchResults[currentMatchIndex].second;
    scroll();

    statusMessage = "Match " + std::to_string(currentMatchIndex + 1) + " of " + std::to_string(searchResults.size());
    statusMessageTime = GetTickCount64();
    force_full_redraw_internal();
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
    force_full_redraw_internal();
}

bool Editor::promptUser(const std::string& prompt, int input_c, std::string& result) {
    if (input_c == 13) { // Enter
        mode = EDIT_MODE;
        force_full_redraw_internal(); // Back to edit mode, redraw content
        return true;
    } else if (input_c == 27) { // Escape
        mode = EDIT_MODE;
        result.clear();
        statusMessage = "Operation cancelled.";
        statusMessageTime = GetTickCount64();
        cursorX = originalCursorX;
        cursorY = originalCursorY;
        rowOffset = originalRowOffset;
        colOffset = originalColOffset;
        force_full_redraw_internal(); // Restore original state, redraw content
        return false;
    } else if (input_c == 8) { // Backspace
        if (!result.empty()) {
            result.pop_back();
        }
    } else if (input_c >= 32 && input_c <= 126) { // Printable characters
        result += static_cast<char>(input_c);
    }
    statusMessage = prompt + result;
    statusMessageTime = GetTickCount64() + 5000; // Keep message alive while typing, but not indefinitely
                                                 // (it's updated on every char, so this is fine)
    return false;
}
void Editor::drawStatusBar() {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole == INVALID_HANDLE_VALUE) return;

    std::string mode_display;
    switch(mode) {
        case EDIT_MODE: mode_display = " EDIT "; break;
        case FILE_EXPLORER_MODE: mode_display = " FILE_EXPLORER_MODE "; break;
        case PROMPT_MODE: mode_display = " PROMPT_MODE "; break;
        case TERMINAL_MODE: mode_display = " TERMINAL_MODE "; break;
        default: mode_display = " UNKNOWN "; break;
    }

    std::string left_aligned_info = mode_display;

    std::string currentStatus;
    std::string filename_display = filename.empty() ? "[No Name]" : filename;
    if (isDirty()) {
        filename_display += "*";
    }

    std::string right_aligned_info = std::to_string(cursorY + 1) + "/" + std::to_string(lines.size()) +
        " Ln" + std::to_string(cursorY + 1) + " Col" + std::to_string(cursorX + 1);

    currentStatus = left_aligned_info + " " + filename_display;

    int padding_needed = screenCols - currentStatus.length() - right_aligned_info.length();
    if (padding_needed > 0) {
        currentStatus += std::string(padding_needed, ' '); 
    } else {
        currentStatus = currentStatus.substr(0, screenCols - right_aligned_info.length());
    }
    currentStatus += right_aligned_info;

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
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole == INVALID_HANDLE_VALUE) return;

    std::string currentMessage = "";
    ULONGLONG currentTime = GetTickCount64();
    if (currentTime < statusMessageTime) {
        currentMessage = trimRight(statusMessage);
    }
    
    if (currentMessage.length() < screenCols) {
        currentMessage += std::string(screenCols - currentMessage.length(), ' ');
    } else if (currentMessage.length() > screenCols) {
        currentMessage = currentMessage.substr(0, screenCols);
    }

    if (currentMessage != prevMessageBarMessage) {
        std::vector<WORD> attributes(screenCols);
        for (int k = 0; k < screenCols; ++k) {
            attributes[k] = YELLOW | INTENSITY | defaultBgColor;
        }

        COORD writePos = {0, (SHORT)(screenRows - 1)};
        DWORD charsWritten;

        WriteConsoleOutputCharacterA(hConsole, currentMessage.c_str(), screenCols, writePos, &charsWritten);
        WriteConsoleOutputAttribute(hConsole, attributes.data(), screenCols, writePos, &charsWritten);

        prevMessageBarMessage = currentMessage;
    }
}

std::string Editor::trimRight(const std::string& s) {
    size_t end = s.find_last_not_of(" \t\n\r\f\v");
    return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}

void Editor::scroll() {
    bool scrolledVertically = false;
    bool scrolledHorizontally = false;

    if (cursorY < rowOffset) {
        rowOffset = cursorY;
        scrolledVertically = true;
    } else if (cursorY >= rowOffset + (screenRows - 2)) {
        rowOffset = cursorY - (screenRows - 2) + 1;
        scrolledVertically = true;
    }

    int renderedCursorX = cxToRx(cursorY, cursorX);
    int effectiveColsForText = screenCols - lineNumberWidth;

    if (renderedCursorX < colOffset) {
        colOffset = renderedCursorX;
        scrolledHorizontally = true;
    } else if (renderedCursorX >= colOffset + effectiveColsForText) {
        colOffset = renderedCursorX - effectiveColsForText + 1;
        scrolledHorizontally = true;
    }

    if (scrolledVertically || scrolledHorizontally) {
        force_full_redraw_internal(); // Only invalidate cache, not clear screen
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
    statusMessageTime = 0;
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

void Editor::deleteChar() {
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

void Editor::deleteForwardChar() {
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
    std::ifstream file(path, std::ios::binary); // Keep binary mode for accurate line ending detection
    if (!file.is_open()) {
        statusMessage = "Error: Could not open file '" + path + "'";
        statusMessageTime = GetTickCount64();
        return false;
    }

    lines.clear();
    std::string line;
    bool crlf_detected_in_file = false;
    bool lf_detected_in_file = false;
    std::vector<std::string> temp_lines;

    while (std::getline(file, line)) { // getline reads until '\n' and discards it
        // Check if the extracted line has a trailing '\r' (indicating CRLF)
        if (!line.empty() && line.back() == '\r') {
            line.pop_back(); // Remove the '\r'
            crlf_detected_in_file = true;
        } else {
            // If no '\r' was at the end, it was an LF-only line
            lf_detected_in_file = true;
        }
        temp_lines.push_back(line);
    }
    file.close();
    lines = temp_lines;

    if (crlf_detected_in_file && !lf_detected_in_file) {
        currentLineEnding = LE_CRLF;
    } else if (lf_detected_in_file && !crlf_detected_in_file) {
        currentLineEnding = LE_LF;
    } else if (crlf_detected_in_file && lf_detected_in_file) {
        currentLineEnding = LE_UNKNOWN; // Mixed line endings
    } else { 
        currentLineEnding = LE_CRLF;
    }

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

    force_full_redraw_internal();

    dirty = false;
    return true;
}
bool Editor::saveFile() {
    if (filename.empty()) {
        show_error("Cannot save: No filename specified. Use Ctrl-O to open/create a file.", 5000);
        return false;
    }

    std::ofstream file(filename);
    if (!file.is_open()) {
        show_error("Could not save file '" + filename + "'", 5000); // CALL MEMBER FUNCTION
        dirty = false;
        return false;
    }

    for (const auto& line : lines) {
        file << line;
        if (currentLineEnding == LE_CRLF) {
            file << '/r/n';
        } else {
            file << '\n';
        }
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
        populateDirectoryEntries(currentDirPath);
        selectedFileIndex = 0;
        fileExplorerScrollOffset = 0;
        // No clearScreen here, refreshScreen handles it on mode change.
        force_full_redraw_internal();
    }
    else { // FILE_EXPLORER_MODE
        mode = EDIT_MODE;
        statusMessage = "Edit Mode: Ctrl-Q = quit | Ctrl-S = save | Ctrl-O = open | Ctrl-E = explorer";
        statusMessageTime = GetTickCount64();
        directoryEntries.clear();
        // No clearScreen here, refreshScreen handles it on mode change.
        force_full_redraw_internal();
    }
}

void Editor::populateDirectoryEntries(const std::string& path) {
    directoryEntries.clear();

    directoryEntries.push_back({ "..", true });

    WIN32_FIND_DATAA findFileData;
    HANDLE hFind = FindFirstFileA((path + "\\*").c_str(), &findFileData);

    if (hFind == INVALID_HANDLE_VALUE) {
        statusMessage = "Error: Could not list directory '" + path + "'";
        statusMessageTime = GetTickCount64();
        return;
    }

    do {
        std::string entryName = findFileData.cFileName;
        if (entryName == "." || entryName == "..") {
            continue;
        }

        bool isDir = (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        directoryEntries.push_back({ entryName, isDir });

    } while (FindNextFileA(hFind, &findFileData) != 0);

    FindClose(hFind);

    std::sort(directoryEntries.begin() + 1, directoryEntries.end(), [](const DirEntry& a, const DirEntry& b) {
        if (a.isDirectory && !b.isDirectory) return true;
        if (!a.isDirectory && b.isDirectory) return false;
        return a.name < b.name;
    });

    currentDirPath = path;
    statusMessage = "Viewing: " + currentDirPath;
    statusMessageTime = GetTickCount64();

    force_full_redraw_internal(); // The content of the explorer has changed
}

void Editor::moveFileExplorerSelection(int key) {
    if (directoryEntries.empty()) return;

    int oldSelectedFileIndex = selectedFileIndex;
    int oldFileExplorerScrollOffset = fileExplorerScrollOffset;

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

    if (selectedFileIndex < fileExplorerScrollOffset) {
        fileExplorerScrollOffset = selectedFileIndex;
    }
    if (selectedFileIndex >= fileExplorerScrollOffset + (screenRows - 4)) {
        fileExplorerScrollOffset = selectedFileIndex - (screenRows - 4) + 1;
    }

    if (oldSelectedFileIndex != selectedFileIndex || oldFileExplorerScrollOffset != fileExplorerScrollOffset) {
        // No clearScreen here. drawFileExplorer will handle partial redraw.
        // But need to ensure refreshScreen is called (which it is, by main loop)
        // and that previous lines for status/message are invalidated.
        prevStatusMessage = ""; // Force status bar redraw
        prevMessageBarMessage = ""; // Force message bar redraw
    }
}

void Editor::handleFileExplorerEnter() {
    if (directoryEntries.empty() || selectedFileIndex < 0 || selectedFileIndex >= directoryEntries.size()) {
        return;
    }

    DirEntry& selectedEntry = directoryEntries[selectedFileIndex];

    char newPathBuffer[MAX_PATH];
    if (PathCombineA(newPathBuffer, currentDirPath.c_str(), selectedEntry.name.c_str()) == NULL) {
        show_error("Invalid path combination.", 5000);
        return;
    }
    std::string newPath = newPathBuffer;

    if (selectedEntry.isDirectory) {
        populateDirectoryEntries(newPath); // This function now calls force_full_redraw_internal()
        selectedFileIndex = 0;
        fileExplorerScrollOffset = 0;
        // No clearScreen() here, populateDirectoryEntries and refreshScreen handles it.
    }
    else {
        openFile(newPath); // This calls force_full_redraw_internal()
        toggleFileExplorer(); // This calls force_full_redraw_internal()
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
    // No clearScreen here. refreshScreen's mode check will handle it.
    force_full_redraw_internal();
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

    if (!CreatePipe(&hChildStdoutRead, &hChildStdoutWrite, &saAttr, 0)) {
        show_error("Stdout pipe creation failed. (GLE: " + std::to_string(GetLastError()) + ")", 5000);
        return;
    }
    if (!SetHandleInformation(hChildStdoutRead, HANDLE_FLAG_INHERIT, 0)) {
        show_error("Stdin pipe creation failed. (GLE: " + std::to_string(GetLastError()) + ")", 5000);
        CloseHandle(hChildStdoutRead); CloseHandle(hChildStdoutWrite);
        return;
    }

    if (!CreatePipe(&hChildStdinRead, &hChildStdinWrite, &saAttr, 0)) {
        show_error("Stdin pipe creation failed. (GLE: " + std::to_string(GetLastError()) + ")", 5000); // CALL MEMBER FUNCTION
        CloseHandle(hChildStdinRead); CloseHandle(hChildStdinWrite);
        CloseHandle(hChildStdoutRead); CloseHandle(hChildStdoutWrite);
        return;
    }

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdError = hChildStdoutWrite;
    si.hStdOutput = hChildStdoutWrite;
    si.hStdInput = hChildStdinRead;
    si.dwFlags |= STARTF_USESTDHANDLES;
    
    // Removed CREATE_NEW_CONSOLE for integrated terminal
    const char* shell_path = "cmd.exe";
    char commandLine[MAX_PATH];
    sprintf_s(commandLine, MAX_PATH, "%s", shell_path);

    BOOL success = CreateProcessA(
        NULL,
        commandLine,
        NULL,
        NULL,
        TRUE,
        0, // dwCreationFlags, set to 0 for integrated terminal
        NULL,
        NULL,
        &si,
        &piProcInfo
    );

    if (!success) {
        show_error("Failed to create child process. (GLE: " + std::to_string(GetLastError()) + ")", 5000); // CALL MEMBER FUNCTION
        CloseHandle(hChildStdinRead); CloseHandle(hChildStdinWrite);
        CloseHandle(hChildStdoutRead); CloseHandle(hChildStdoutWrite);
        return;
    }

    CloseHandle(hChildStdinRead);
    CloseHandle(hChildStdoutWrite);
    
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
    
    force_full_redraw_internal(); // Invalidate cache and trigger redraw for terminal content
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
}

void Editor::writeTerminalInput(const std::string& input) {
    if (hChildStdinWrite == NULL) return;
    DWORD bytesWriten;

    WriteFile(hChildStdinWrite, input.c_str(), input.length(), &bytesWriten, NULL);
}

void Editor::processTerminalOutput(const std::string& data) {
    for (char c : data) {
        if (!asiEscapeBuffe.empty()) {
            asiEscapeBuffe += c;
            if (c >= '@' && c <= '~') {
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
                            } catch (...) { }
                        }
                    }

                    char final_char = asiEscapeBuffe.back();
                    if (final_char == 'm') {
                        applyAnsiSGR(ansiSGRParams);
                    } else if (final_char == 'H' || final_char == 'f') {
                        int row = ansiSGRParams.empty() ? 1 : ansiSGRParams[0];
                        int col = (ansiSGRParams.size() < 2) ? 1 : ansiSGRParams[1];
                        applyAnsiCUP(row, col);
                    } else if (final_char == 'J') {
                        int param = ansiSGRParams.empty() ? 0 : ansiSGRParams[0];
                        applyAnsiED(param);
                    } else if (final_char == 'K') {
                        int param = ansiSGRParams.empty() ? 0 : ansiSGRParams[0];
                        applyAnsiEL(param);
                    } else if (final_char == 'A') {
                         int num = ansiSGRParams.empty() ? 1 : ansiSGRParams[0];
                         terminalCursorY = std::max(0, terminalCursorY - num);
                    } else if (final_char == 'B') {
                         int num = ansiSGRParams.empty() ? 1 : ansiSGRParams[0];
                         terminalCursorY = std::min(terminalHeight - 1, terminalCursorY + num);
                    } else if (final_char == 'C') {
                         int num = ansiSGRParams.empty() ? 1 : ansiSGRParams[0];
                         terminalCursorX = std::min(terminalWidth - 1, terminalCursorX + num);
                    } else if (final_char == 'D') {
                         int num = ansiSGRParams.empty() ? 1 : ansiSGRParams[0];
                         terminalCursorX = std::max(0, terminalCursorX - num);
                    }
                }
                asiEscapeBuffe.clear();
            }
        } else if (c == 0x1B) {
            asiEscapeBuffe += c;
        } else if (c == '\r') {
            terminalCursorX = 0;
        } else if (c == '\n') {
            terminalCursorY++;
            terminalCursorX = 0;
            if (terminalCursorY >= terminalHeight) {
                for (int r = 0; r < terminalHeight - 1; ++r) {
                    terminalBuffer[r] = terminalBuffer[r+1];
                }
                terminalBuffer[terminalHeight-1].assign(terminalWidth, { ' ', currentFgColor, currentBgColor, currentBold });
                terminalCursorY = terminalHeight - 1;
            }
        } else if (c == '\b') {
            if (terminalCursorX > 0) {
                terminalCursorX--;
                if (terminalCursorY < terminalHeight && terminalCursorX < terminalWidth) {
                    terminalBuffer[terminalCursorY][terminalCursorX] = { ' ', currentFgColor, currentBgColor, currentBold };
                }
            }
        } else if (c == '\t') {
            terminalCursorX += (KILO_TAB_STOP - (terminalCursorX % KILO_TAB_STOP));
            if (terminalCursorX >= terminalWidth) terminalCursorX = terminalWidth - 1;
        } else {
            if (terminalCursorY < terminalHeight && terminalCursorX < terminalWidth) {
                terminalBuffer[terminalCursorY][terminalCursorX] = { c, currentFgColor, currentBgColor, currentBold };
            }
            terminalCursorX++;
            if (terminalCursorX >= terminalWidth) {
                terminalCursorX = 0;
                terminalCursorY++;
                if (terminalCursorY >= terminalHeight) {
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
    terminalCursorX = 0;
    terminalCursorY = 0;
    terminalScrollOffset = 0;
    currentFgColor = defaultFgColor;
    currentBgColor = defaultBgColor;
    currentBold = false;
    ansiSGRParams.clear();
    asiEscapeBuffe.clear();
    clearTerminalBuffer();
    
    for (auto& s : prevDrawnLines)
        s.assign(screenCols, ' ');
    prevStatusMessage = "";
    prevMessageBarMessage = "";
}

void Editor::drawTerminalScreen() {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole == INVALID_HANDLE_VALUE) {
        return;
    }

    for (int y = 0; y < terminalHeight; ++y) {
        std::string lineContent;
        lineContent.reserve(terminalWidth);
        std::vector<WORD> lineAttributes(terminalWidth);

        for (int x = 0; x < terminalWidth; ++x) {
            TerminalChar tc = terminalBuffer[y][x];
            lineContent += tc.c;
            WORD attributes = tc.fgColor | tc.bgColor;
            if (tc.bold) attributes |= FOREGROUND_INTENSITY;
            lineAttributes[x] = attributes;
        }

        COORD writePos = { 0, (SHORT)y };

        DWORD charsWritten;
        if (!WriteConsoleOutputCharacterA(hConsole, lineContent.c_str(), terminalWidth, writePos, &charsWritten)) {
            std::cerr << "Terminal: WriteConsoleOutputCharacterA failed: " << GetLastError() << std::endl;
        }

        if (!WriteConsoleOutputAttribute(hConsole, lineAttributes.data(), terminalWidth, writePos, &charsWritten)) {
            std::cerr << "Terminal: WriteConsoleOutputAttribute failed: " << GetLastError() << std::endl;
        }
    }
}

void Editor::clearTerminalBuffer() {
    terminalBuffer.assign(terminalHeight, std::vector<TerminalChar>(terminalWidth, { ' ', defaultFgColor, defaultBgColor, false }));
}

void Editor::applyAnsiSGR(const std::vector<int>& params) {
  if (params.empty()) {
    currentFgColor = defaultFgColor;
    currentBgColor = defaultBgColor;
    currentBold = false;
  }
  for (int param : params) {
    switch (param) {
      case 0:
        currentFgColor = defaultFgColor;
        currentBgColor = defaultBgColor;
        currentBold = false;
        break;
      case 1:
        currentBold = true;
        break;
      case 22:
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
        break;
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
        break;
      default:
        break;
    }
  }
}

void Editor::applyAnsiCUP(int row, int col) {
  terminalCursorY = std::max(0, row - 1);
  terminalCursorX = std::max(0, col - 1);

  if (terminalCursorY >= terminalHeight)
    terminalCursorY = terminalHeight - 1;
  if (terminalCursorX >= terminalWidth)
    terminalCursorX = terminalWidth - 1;
}

void Editor::applyAnsiED(int param) {
  if (param == 0) {
    for (int x = terminalCursorX; x < terminalWidth; ++x) {
      terminalBuffer[terminalCursorY][x] = {' ', currentFgColor, currentBgColor,
                                            currentBold};
    }
    for (int y = terminalCursorY + 1; y < terminalHeight; ++y) {
      terminalBuffer[y].assign(
          terminalWidth, {' ', currentFgColor, currentBgColor, currentBold});
    }
  } else if (param == 1) {
    for (int x = 0; x <= terminalCursorX; ++x) {
      terminalBuffer[terminalCursorY][x] = {' ', currentFgColor, currentBgColor,
                                            currentBold};
    }
    for (int y = 0; y < terminalCursorY; ++y) {
      terminalBuffer[y].assign(
          terminalWidth, {' ', currentFgColor, currentBgColor, currentBold});
    }
  } else if (param == 2) {
    clearTerminalBuffer();
  }
}

void Editor::applyAnsiEL(int param) {
  if (param == 0) {
    for (int x = terminalCursorX; x < terminalWidth; ++x) {
      terminalBuffer[terminalCursorY][x] = {' ', currentFgColor, currentBgColor,
                                            currentBold};
    }
  } else if (param == 1) {
    for (int x = 0; x <= terminalCursorX; ++x) {
      terminalBuffer[terminalCursorY][x] = {' ', currentFgColor, currentBgColor,
                                            currentBold};
    }
  } else if (param == 2) {
    terminalBuffer[terminalCursorY].assign(
        terminalWidth, {' ', currentFgColor, currentBgColor, currentBold});
  }
}

void Editor::refreshScreen() {
    setCursorVisibility(false);

    // Check if mode has changed since last render or initial draw
    if (mode != lastRenderedMode) {
        clearScreen(); // ONLY clear the screen if mode has just changed
        // No explicit force_full_redraw_internal here, as it's implied by clearing
        // and the fact that the draw function will write everything anew.
        // However, force_full_redraw_internal is safe here if desired, as it only invalidates.
        force_full_redraw_internal(); // Invalidate cache for new mode's content
        lastRenderedMode = mode; // Update last rendered mode
    }

    updateScreenSize(); // Ensure dimensions are up-to-date and prevDrawnLines resized

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
        finalCursorY = screenRows - 1;
        finalCursorX = std::min(finalCursorX, screenCols - 1);
    } else if (mode == TERMINAL_MODE) {
        finalCursorX = terminalCursorX;
        finalCursorY = terminalCursorY;
        finalCursorX = std::max(0, std::min(finalCursorX, screenCols - 1));
        finalCursorY = std::max(0, std::min(finalCursorY, screenRows - 3));
    }
    else {
        int explorer_content_start_row = 2;
        finalCursorX = 0;
        finalCursorY = explorer_content_start_row + (selectedFileIndex - fileExplorerScrollOffset);

        finalCursorY = std::max(explorer_content_start_row, std::min(finalCursorY, screenRows - 3));
        finalCursorX = std::min(finalCursorX, screenCols - 1);
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

    if (!FillConsoleOutputCharacterA(hConsole,
                                     (TCHAR)' ',
                                     cellsCount,
                                     homeCoords,
                                     &charsWritten))
    {
        std::cerr << "Error: FillConsoleOutputCharacterA failed in clearScreen. GLE: " << GetLastError() << std::endl;
        return;
    }

    if (!FillConsoleOutputAttribute(hConsole,
                                    defaultFgColor | defaultBgColor,
                                    cellsCount,
                                    homeCoords,
                                    &charsWritten))
    {
        std::cerr << "Error: FillConsoleOutputAttribute failed in clearScreen. GLE: " << GetLastError() << std::endl;
        return;
    }

    SetConsoleCursorPosition(hConsole, homeCoords);
}

void Editor::force_full_redraw_internal() {
    for (auto& s : prevDrawnLines) {
        s.assign(screenCols, ' ');
    }

    prevStatusMessage = "";
    prevMessageBarMessage = "";
}

bool Editor::setConsoleFont(const ConsoleFontInfo& fontInfo) {
    HANDLE hConsoleOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsoleOutput == INVALID_HANDLE_VALUE) {
        show_error("Invalid console output handle for font setting.", 3000); // CALL MEMBER FUNCTION
        return false;
    }

    CONSOLE_FONT_INFOEX cfi;
    cfi.cbSize = sizeof(cfi);
    if (!GetCurrentConsoleFontEx(hConsoleOutput, FALSE, &cfi)) {
        show_error("GetCurrentConsoleFontEx failed for font setting. GLE: " + std::to_string(GetLastError()), 3000); // CALL MEMBER FUNCTION
        return false;
    }

    cfi.dwFontSize.X = fontInfo.fontSizeX;
    cfi.dwFontSize.Y = fontInfo.fontSizeY;

    // Convert std::string (MultiByte) to WCHAR (WideChar)
    MultiByteToWideChar(CP_ACP, 0, fontInfo.name.c_str(), -1, cfi.FaceName, LF_FACESIZE);

    if (!SetCurrentConsoleFontEx(hConsoleOutput, FALSE, &cfi)) {
        show_error("SetCurrentConsoleFontEx failed. GLE: " + std::to_string(GetLastError()), 3000); // CALL MEMBER FUNCTION
        return false;
    }

    updateScreenSize(); // This will recalculate screenRows/Cols based on new font size
    force_full_redraw_internal(); // Force a full visual update with new dimensions

    currentFont = fontInfo;
    statusMessage = "Font set to " + fontInfo.name + " [" + std::to_string(fontInfo.fontSizeX) + "x" + std::to_string(fontInfo.fontSizeY) + "]";
    statusMessageTime = GetTickCount64() + 3000;
    return true;
}

ConsoleFontInfo Editor::getCurrentConsoleFont() {
    HANDLE hConsoleOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsoleOutput == INVALID_HANDLE_VALUE) {
        return {"ErrorFont", 0, 0};
    }

    CONSOLE_FONT_INFOEX cfi;
    cfi.cbSize = sizeof(cfi);
    if (!GetCurrentConsoleFontEx(hConsoleOutput, FALSE, &cfi)) {
        return {"ErrorFont", 0, 0};
    }

    char buffer[LF_FACESIZE];
    WideCharToMultiByte(CP_ACP, 0, cfi.FaceName, -1, buffer, LF_FACESIZE, NULL, NULL);

    return {std::string(buffer), cfi.dwFontSize.X, cfi.dwFontSize.Y};
}

std::vector<ConsoleFontInfo> Editor::getAvailableConsoleFonts() {
    FontEnumData data;

    HDC hdc = GetDC(NULL);
    if (!hdc) {
        show_error("Could not get screen device context for fonts.", 3000); // CALL MEMBER FUNCTION
        return {};
    }

    LOGFONT lf = {0};
    lf.lfCharSet = DEFAULT_CHARSET;
    std::wcscpy(lf.lfFaceName, L""); // Enumerate all fonts

    EnumFontFamiliesEx(hdc, &lf, (FONTENUMPROC)EnumFontFamExProc, (LPARAM)&data, 0);

    ReleaseDC(NULL, hdc);

    // Filter for uniqueness
    std::sort(data.fonts.begin(), data.fonts.end(), [](const ConsoleFontInfo& a, const ConsoleFontInfo& b){
        return a.name < b.name;
    });
    data.fonts.erase(std::unique(data.fonts.begin(), data.fonts.end(), [](const ConsoleFontInfo& a, const ConsoleFontInfo& b){
        return a.name == b.name;
    }), data.fonts.end());


    // Filter for common console-friendly fonts for a more curated list
    std::vector<ConsoleFontInfo> commonFonts;
    for(const auto& font : data.fonts) {
        if (font.name.find("Consolas") != std::string::npos ||
            font.name.find("Lucida Console") != std::string::npos ||
            font.name.find("Cascadia Code") != std::string::npos ||
            font.name.find("Fira Code") != std::string::npos ||
            font.name.find("Inconsolata") != std::string::npos ||
            font.name.find("Ubuntu Mono") != std::string::npos ||
            font.name.find("Source Code Pro") != std::string::npos)
        {
            commonFonts.push_back({font.name, 0, 0});
        }
    }
    // Fallback if no common fonts are found (e.g., on a minimal system)
    if (commonFonts.empty() && !data.fonts.empty()) {
        // Add all unique fonts if no common ones found, as a fallback
        commonFonts = data.fonts;
    } else if (commonFonts.empty()) {
        // Add some absolute defaults if enumeration failed or found nothing
        commonFonts.push_back({"Consolas", 0, 0});
        commonFonts.push_back({"Lucida Console", 0, 0});
    }

    return commonFonts;
}