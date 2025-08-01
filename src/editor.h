#pragma once

#include "common.h"
#include <algorithm>
#include <sstream>
#include <vector>
#include <string>
#include <filesystem>
#include <map>
#include <functional>
#include <nlohmann/json.hpp>

enum EditorMode {
	EDIT_MODE,
	FILE_EXPLORER_MODE,
	PROMPT_MODE,
	TERMINAL_MODE
};

struct KeyCombination {
    int keyCode;
    bool ctrl;
    bool alt;
    bool shift;

    bool operator<(const KeyCombination& other) const {
        if (keyCode != other.keyCode) return keyCode < other.keyCode;
        if (ctrl != other.ctrl) return ctrl < other.ctrl;
        if (alt != other.alt) return alt < other.alt;
        return shift < other.shift;
    }

	std::string toString() const {
		std::string s = "";
		if (ctrl) s += "Crtl+";
		if (alt) s += "Alt+";
		if (shift) s += "Shift+";
		if (keyCode >= 32 && keyCode <= 126) {
			s += static_cast<char>(keyCode);
		}
		else {
			switch (keyCode) {
			case VK_UP: s += "Up"; break;
			case VK_DOWN: s += "Down"; break;
			case VK_LEFT: s += "Left"; break;
			case VK_RIGHT: s += "Right"; break;
			case VK_PRIOR: s += "PageUp"; break; // VK_PRIOR is Page Up
			case VK_NEXT: s += "PageDown"; break; // VK_NEXT is Page Down
			case VK_HOME: s += "Home"; break;
			case VK_END: s += "End"; break;
			case VK_DELETE: s += "Delete"; break; // VK_DELETE is Delete key
			case VK_BACK: s += "Backspace"; break; // VK_BACK is Backspace
			case VK_RETURN: s += "Enter"; break; // VK_RETURN is Enter
			case VK_ESCAPE: s += "Esc"; break;
			case VK_TAB: s += "Tab"; break;
			case VK_F1: s += "F1"; break; // ... F1-F12
			case VK_F2: s += "F2"; break;
				// ... add more VK_ codes as needed
			default: s += "KEY_" + std::to_string(keyCode); break;
			}
		}
		return s;
	}
}; 

struct lua_State;

struct DirEntry {
	std::string name;
	bool isDirectory;
};

enum EditorCommand {
	CMD_QUIT = 'q',
	CMD_SAVE = 19,
	CMD_OPEN = 15,
	CMD_UNKNOWN = 0
};

const int KILO_TAB_STOP = 8;

struct TerminalChar {
	char c;

	int fgColor;
	int bgColor;
	bool bold;
};

struct ConsoleFontInfo {
    std::string name;
    short fontSizeX;
    short fontSizeY;
};

enum TextStyleFlags {
	STYLE_NONE = 0,
	STYLE_BOLD = 1 << 0,
	STYLE_ITALIC = 1 << 1,
	STYLE_UNDERLINE = 1 << 2,
};

struct TextStyling {
	int startCol;
	int endCol;
	unsigned int fgColor;
	unsigned int bgColor;
	int styleFlags;
};

enum TextDecorationType {
	DECORATION_NONE,
	DECORATION_ERROR_UNDERLINE,
	DECORATION_WARNING_UNDERLINE,
	DECORATION_INFO_OVERLAY,    // e.g., for semantic highlights, dimming
	DECORATION_MATCH_HIGHLIGHT, // e.g., for search results
	// Add more as needed (e.g., gutter icons, line numbers with backgrounds)
};

struct TextDecoration {
	std::string id;       // Unique ID for this decoration instance
	int startCol;         // 1-based start column
	int endCol;           // 1-based end column (exclusive)
	TextDecorationType type; // Type of decoration
	std::string tooltip;  // Message for tooltip on hover
	unsigned int color;   // Primary color for the decoration (e.g., underline color)
	// Add more properties as needed, like `bgColor` for overlays
};

struct Editor {
public:
    static std::map<std::string, std::string> plugin_data_storage;
	std::vector<std::string> lines;
	std::string filename;
	int cursorX;
	int cursorY;
	int screenRows;
	int screenCols;
	int rowOffset;
	int colOffset;
	bool dirty;
	int lineNumberWidth;
	std::string statusMessage;
	ULONGLONG statusMessageTime;

    enum LineEnding {
        LE_CRLF, // \r\n (Windows)
        LE_LF,   // \n   (Unix, macOS, Linux)
        LE_UNKNOWN // Mixed or not yet detected
    };
    LineEnding currentLineEnding = LE_CRLF;

	// Keyboard events / Custom binds
	std::map<KeyCombination, std::string> customKeybindings;
	std::map < std::string, std::function<void()>> commandRegistry;

	void registerEditorCommand(const std::string& name, std::function<void()> func);
	bool loadKeybindings(const std::string& filePath = "keybindings.json");
	void setupDefaultKeybindings(); // Hardcoded defaults
	void executeCommand(const std::string& commandName);

	lua_State* L;

	std::vector<std::string> prevDrawnLines;
	std::string prevStatusMessage;
	std::string prevMessageBarMessage;

	EditorMode mode;
	std::string currentDirPath;
	std::vector<DirEntry> directoryEntries;
	int selectedFileIndex;
	int fileExplorerScrollOffset;

	std::string searchQuery;
	std::vector<std::pair<int, int>> searchResults;
	int currentMatchIndex;
	int originalCursorX, originalCursorY;
	int originalRowOffset, originalColOffset;
	std::string promptMessage;

	bool terminalActive;
	std::vector<std::vector<TerminalChar>> terminalBuffer;
	int terminalCursorX;
	int terminalCursorY;
	int terminalScrollOffset;
	int terminalHeight;
	int terminalWidth;

	HANDLE hChildStdinRead;
	HANDLE hChildStdinWrite;
	HANDLE hChildStdoutRead;
	HANDLE hChildStdoutWrite;
	PROCESS_INFORMATION piProcInfo;
    HANDLE hConsoleInput;
    DWORD originalConsoleMode;

	std::string asiEscapeBuffe;
	std::vector<int> ansiSGRParams;
	int currentFgColor;
	int currentBgColor;
	bool currentBold;
	WORD defaultFgColor;
	WORD defaultBgColor;
    EditorMode lastRenderedMode = EDIT_MODE;

    ConsoleFontInfo currentFont;
	std::map<int, std::vector<TextStyling>> lineStyling;
	std::map<int, std::vector<TextDecoration>> lineDecorations;

	Editor();
	~Editor();

	void updateScreenSize();
	void refreshScreen();
	void moveCursor(int key);
	void insertChar(int c);
	void insertNewline();
	void deleteChar();
	void deleteForwardChar();
	bool openFile(const std::string& path);
	bool saveFile();
	bool isDirty() const { return dirty; };

	void toggleFileExplorer();
	void populateDirectoryEntries(const std::string& path);
	void moveFileExplorerSelection(int key);
	void handleFileExplorerEnter();

	void startSearch();
	void  performSearch();
	void findNext();
	void findPrevious();

	bool promptUser(const std::string& prompt, int input_c, std::string& result);

	void initializeLua();
    void finalizeLua();
    void exposeEditorToLua();
    void loadLuaPlugins(const std::string& pluginDir = "./plugins");
    bool executeLuaPluginCommand(const std::string& pluginName, const std::string& commandName);

    struct LuaCallback {
        int funcRef;
        lua_State* L_state;
    };

    std::vector<LuaCallback> onKeyPressCallbacks;
    std::vector<LuaCallback> onFileOpenedCallbacks;
    std::vector<LuaCallback> onFileSavedCallbacks;
    std::vector<LuaCallback> onBufferChangedCallbacks;
    std::vector<LuaCallback> onCursorMovedCallbacks;
    std::vector<LuaCallback> onModeChangedCallbacks;

    bool ctrl_pressed = false;
    std::string promptResult;
    void force_full_redraw_internal();
    void clearScreen();
	void setCursorVisibility(bool visible);

	void toggleTerminal();
	void startTerminal();
	void stopTerminal();
	void readTerminalOutput();
	void writeTerminalInput(const std::string& input);
	void processTerminalOutput(const std::string& data);
	void resizeTerminal(int width, int height);
	void drawTerminalScreen();
	void clearTerminalBuffer();
	void applyAnsiSGR(const std::vector<int>& params);
	void applyAnsiCUP(int row, int col);
	void applyAnsiED(int param);
    void scroll();
	void applyAnsiEL(int param);
    void triggerEvent(const std::string& eventName, int param = 0);
    void triggerEvent(const std::string& eventName, const std::string& param);
	void calculateLineNumberWidth();

    bool setConsoleFont(const ConsoleFontInfo& fontInfo);
    ConsoleFontInfo getCurrentConsoleFont();
    std::vector<ConsoleFontInfo> getAvailableConsoleFonts();

    int kiloTabStop = KILO_TAB_STOP;
    void show_error(const std::string& message, ULONGLONG duration_ms = 8000);
	void show_message(const std::string& message, ULONGLONG duration_ms = 8000);
	void processInput(int raw_key_code, char ascii_char, DWORD control_key_state);

    bool should_exit = false;

	void setTextForegroundColor(int lineNum, int startCol, int endCol, unsigned int rgbColor);
	void setTextBackgroundColor(int lineNum, int startCol, int endCol, unsigned int rgbColor); // Optional, but good to have
	void setTextStyles(int lineNum, int startCol, int endCol, int styleFlags);
	void clearLineStyling(int lineNum, int startCol = 1, int endCol = -1); // -1 means until end of line
	void addTextDecoration(const std::string& id, int lineNum, int startCol, int endCol, TextDecorationType type, const std::string& tooltip = "", unsigned int color = 0);
	void clearDecorations(const std::string& idPrefix = ""); // Clear all if prefix is empty
	void showTooltip(int screenX, int screenY, const std::string& message, ULONGLONG duration_ms);

private:
	void drawScreenContent();
	void drawStatusBar();
	void drawMessageBar();

	std::string trimRight(const std::string& s);

	int cxToRx(int lineIndex, int cx);
	int rxToCx(int lineIndex, int rx);
	std::string getRenderedLine(int fileRow);

	void drawFileExplorer();

	void applyStylingInternal(int lineNum, int startCol, int endCol, std::function<void(TextStyling&)> applyFunc);
};