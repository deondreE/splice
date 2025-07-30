#pragma once

#include "common.h"
#include <algorithm>
#include <sstream>
#include <vector>
#include <string>
#include <filesystem>
#include <map>

enum EditorMode {
	EDIT_MODE,
	FILE_EXPLORER_MODE,
	PROMPT_MODE,
	TERMINAL_MODE
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

private:
	void drawScreenContent();
	void drawStatusBar();
	void drawMessageBar();

	std::string trimRight(const std::string& s);

	int cxToRx(int lineIndex, int cx);
	int rxToCx(int lineIndex, int rx);
	std::string getRenderedLine(int fileRow);

	void drawFileExplorer();
};