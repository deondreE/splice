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

struct Editor {
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

	std::string asiEscapeBuffe;
	std::vector<int> ansiSGRParams;
	int currentFgColor;
	int currentBgColor;
	bool currentBold;

	int defaultFgColor;
	int defaultBgColor;

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
	bool isDirty() const { return dirty; }

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
	void loadLuaPlugins(const std::string& pluginDir = "plugins");
	bool executeLuaPluginCommand(const std::string& pluginName, const std::string& commandName);
	void exposeEditorToLua();

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
	void applyAnsiEL(int param);

private:
	void calculateLineNumberWidth();
	void drawScreenContent();
	void drawStatusBar();
	void drawMessageBar();
	void scroll();
	std::string trimRight(const std::string& s);

	int cxToRx(int lineIndex, int cx);
	int rxToCx(int lineIndex, int rx);
	std::string getRenderedLine(int fileRow);

	void drawFileExplorer();
};