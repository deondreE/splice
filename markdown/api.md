# Lua API Reference

Here's a comprehensive list of functions and properties available in the editor_api global table, along with their usage and descriptions:

editor_api Table

## Editor State & Information

- editor.set_status_message(message, [duration_ms])
  - Sets the text displayed in the editor's status bar.
  - message (string): The message to display.
  - duration_ms (integer, optional): How long the message should be displayed in milliseconds. Defaults to 5000ms (5 seconds).
- editor.show_message(message, [duration_ms])
  - Similar to set_status_message, but semantically intended for general, transient information messages.
  - message (string): The message to display.
  - duration_ms (integer, optional): Duration in milliseconds. Defaults to 5000ms.
- editor.show_error(message, [duration_ms])
  - Displays an error message in the status bar, potentially with different styling (e.g., red text).
  - message (string): The error message.
  - duration_ms (integer, optional): Duration in milliseconds. Defaults to 8000ms.
- editor.get_current_time_ms()
  - Returns (integer): The current system tick count in milliseconds. Useful for timing.
- editor.refresh_screen()
  - Triggers a redraw of the editor's display. Useful after performing multiple text operations or if the UI seems out of sync.
- editor.force_full_redraw()
  - Forces a complete redraw of the entire console. This clears all cached screen content and ensures a pristine refresh. Use for major UI changes or mode switches.
- editor.get_screen_size()
  - Returns (integer, integer): width, height representing the dimensions of the editor's primary drawing area (excluding status and message bars).
- editor.is_ctrl_pressed()
  - Returns (boolean): true if the Ctrl key is currently held down, false otherwise.

## Text Buffer Manipulation

- editor.get_line_count()
  - Returns (integer): The total number of lines in the current editor buffer.
- editor.get_current_line_text()
  - Returns (string): The content of the line where the cursor is currently located.
- editor.get_line(line_number)
  - line_number (integer): A 1-based index of the line to retrieve.
  - Returns (string): The content of the specified line. Returns an empty string if line_number is out of bounds.
- editor.set_line(line_number, text)
  - line_number (integer): A 1-based index of the line to modify.
  - text (string): The new content for the specified line.
- editor.insert_line(line_number, text)
  - line_number (integer): A 1-based index where the new line will be inserted. Lines at or after this index will be shifted down.
  - text (string): The content of the new line.
- editor.delete_line(line_number)
  - line_number (integer): A 1-based index of the line to delete.
- editor.insert_text(text)
  - text (string): The string to insert at the current cursor position. Newline characters (\n) within the string will correctly trigger new line insertions in the buffer.
- editor.get_buffer_content()
  - Returns (table): A Lua table where each element is a string representing a line in the buffer. The table is 1-based indexed.
- editor.set_buffer_content(content_table)
  - content_table (table): A Lua table of strings. Each string will become a line in the editor buffer, replacing all existing content.
- editor.get_char_at(line_number, col_number)
  - line_number (integer): A 1-based line index.
  - col_number (integer): A 1-based column index.
  - Returns (string): The single character at the specified logical position, or nil if the coordinates are out of bounds.
- editor.get_tab_stop_width()
  - Returns (integer): The number of spaces a tab character (\t) expands to for display and cursor movement.

## Cursor & View Management

- editor.get_cursor_x()
  - Returns (integer): The 1-based column index of the cursor's current position.
- editor.get_cursor_y()
  - Returns (integer): The 1-based line index of the cursor's current position.
- editor.set_cursor_position(x, y)
  - x (integer): The 1-based target column index for the cursor.
  - y (integer): The 1-based target line index for the cursor.
- editor.set_row_offset(offset)
  - offset (integer): Sets the 0-based index of the top-most visible line in the editor's content area.
- editor.set_col_offset(offset)
  - offset (integer): Sets the 0-based index of the left-most visible column in the editor's content area.
- editor.get_row_offset()
  - Returns (integer): The current 0-based index of the top-most visible line.
- editor.get_col_offset()
  - Returns (integer): The current 0-based index of the left-most visible column.
- editor.center_view_on_cursor()
  - Adjusts the editor's scroll offsets (row_offset, col_offset) to ensure the cursor is fully visible on screen, attempting to center it if possible.

## File System & Editor Actions

- editor.get_current_filename()
  - Returns (string): The full path of the currently open file. Returns an empty string ("") if no file is currently loaded.
- editor.open_file(path)
  - path (string): The full file path to attempt to open in the editor.
  - Returns (boolean): true if the file was opened successfully, false otherwise.
- editor.save_file()
  - Saves the content of the current editor buffer to its associated filename.
  - Returns (boolean): true on successful save, false if saving failed (e.g., no filename, permissions).
- editor.is_dirty()
  - Returns (boolean): true if the current buffer has unsaved changes, false otherwise.
- editor.get_directory_path()
  - Returns (string): The current working directory displayed by the file explorer.
- editor.set_directory_path(path)
  - path (string): The directory path to switch the file explorer view to.
- editor.list_directory([path])
  - path (string, optional): The directory to list. If omitted, lists the get_directory_path() directory.
  - Returns (table, string): On success, returns a Lua table where each element is a sub-table representing a directory entry. On failure, returns nil followed by an error message.
    - Each entry sub-table has two fields:
      - name (string): The name of the file or directory.
      - is_directory (boolean): true if the entry is a directory, false if it's a file.
- editor.create_file(path)
  - path (string): The full path including the filename for the new file to create.
  - Returns (boolean, string): true on success, or false and an error message string on failure.
- editor.create_directory(path)
  - path (string): The full path for the new directory to create.
  - Returns (boolean, string): true on success, or false and an error message string on failure.
- editor.delete_file(path)
  - path (string): The full path of the file to delete.
  - Returns (boolean, string): true on success, or false and an error message string on failure.
- editor.delete_directory(path)
  - path (string): The full path of the directory to delete. The directory must be empty.
  - Returns (boolean, string): true on success, or false and an error message string on failure.

## Terminal Interaction

- editor.send_terminal_input(text)
  - text (string): Sends the given text as input to the integrated terminal.
- editor.toggle_terminal()
  - Switches between editor and integrated terminal modes.

## User Interaction & Prompts

- editor.prompt(prompt_message, [default_value])
  - prompt_message (string): The message to display to the user in the message bar.
  - default_value (string, optional): A pre-filled string for the user to edit.
  - Returns (boolean): true if the prompt mode was successfully initiated.
  - Note: This function currently initiates a prompt in the editor's message bar and returns immediately. To get the user's input back into Lua, you would need to use a more advanced pattern (e.g., C++ callbacks to Lua, or Lua coroutines managed by C++). The current implementation does not block Lua execution until the user presses Enter.

## Font Controls (NEW)

- editor.set_console_font(font_name, font_size_x, font_size_y)
  - Sets the console's font family and character cell size. This affects the entire console window.
  - font_name (string): The name of the font (e.g., "Consolas", "Lucida Console", "Cascadia Code").
  - font_size_x (integer): The desired width of a character cell in pixels.
  - font_size_y (integer): The desired height of a character cell in pixels.
  - Returns (boolean): true if the font was set successfully, false otherwise.
  - Note: The console host might adjust the requested size to the closest available matching font.
- editor.get_current_console_font()
  - Returns (string, integer, integer): font_name, font_size_x, font_size_y representing the currently active console font's name and its actual character cell dimensions in pixels.
- editor.get_available_console_fonts()
  - Returns (table): A Lua table of available fixed-pitch, TrueType fonts suitable for console display on the system.
    - Each element in the table is a sub-table with:
      - name (string): The font's name.
      - size_x (integer): Placeholder for X size (usually 0, actual size negotiated by console).
      - size_y (integer): Placeholder for Y size (usually 0, actual size negotiated by console).
    - Note: The sizes returned here are often placeholder 0s from the enumeration; the console itself determines the valid sizes a font can be set to. Use set_console_font with desired pixel sizes, and get_current_console_font to confirm the applied size.

## Helper Functions

- editor.path_join(part1, part2, ...)
  - Takes a variable number of string arguments, concatenating them into a valid path using platform-specific separators.
  - Returns (string): The combined path.
- editor.file_exists(path)
  - path (string): The path to check.
  - Returns (boolean): true if a file (not a directory) exists at the given path, false otherwise.
- editor.is_directory(path)
  - path (string): The path to check.
  - Returns (boolean): true if a directory exists at the given path, false otherwise.
- editor.execute_command(command_string)
  - command_string (string): A command string to execute in a system shell.
  - Returns (integer): The exit code of the executed command.
  - Note: This is a blocking call and typically won't capture command output directly within the editor UI. Use editor.send_terminal_input() for interactive terminal commands.

## Plugin Data Persistence

- editor.save_plugin_data(key, value_string)
  - key (string): A unique identifier for the data.
  - value_string (string): The data to save. Currently, only string values are directly supported for simplicity (you can serialize Lua tables to JSON strings yourself).
  - Returns (boolean): true on success, false on failure.
  - Note: In the current implementation, this stores data in an in-memory map. For real persistence across editor sessions, this would need to write to a plugin-specific file (e.g., plugins/data/my_plugin_key.txt).
- editor.load_plugin_data(key)
  - key (string): The unique identifier for the data to load.
  - Returns (string, or nil): The previously saved string data, or nil if no data is found for the key.

## Event Handling

Plugins can register Lua functions to be called when specific events occur in the editor. This allows for powerful and reactive plugin behavior.

- editor.register_event(event_name, handler_function) - event_name (string): The name of the event to subscribe to. - handler_function (function): The Lua function to be called when the event occurs. The arguments passed to this function depend on the event.
  Available Events:

- "on_key_press"
  - Handler Function Signature: function(key_code)
  - key_code (integer): The integer representation of the key pressed.
  - Return Value: The handler should return true if it fully handled the key press (preventing default editor processing), or false otherwise.
- "on_file_opened"
  - Handler Function Signature: function(filename)
  - filename (string): The full path of the file that was just opened.
- "on_file_saved"
  - Handler Function Signature: function(filename)
  - filename (string): The full path of the file that was just saved.
- "on_buffer_changed"
  - Handler Function Signature: function()
  - Called after any modification to the editor's text buffer (e.g., character insertion, deletion, line changes).
- "on_cursor_moved"
  - Handler Function Signature: function()
  - Called whenever the editor's cursor position changes.
- "on_mode_changed"
  - Handler Function Signature: function(new_mode_id)
  - new_mode_id (integer): An integer representing the new editor mode (e.g., Edit, File Explorer, Terminal, Prompt).
