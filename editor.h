#pragma once

#include "common.h"
#include <algorithm>
#include <sstream>
#include <vector>
#include <string>
#include <filesystem>

enum EditorMode {
	EDIT_MODE,
	FILE_EXPLORER_MODE,
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

	std::vector<std::string> prevDrawnLines;
	std::string prevStatusMessage;
	std::string prevMessageBarMessage;

	EditorMode mode;
	std::string currentDirPath;
	std::vector<DirEntry> directoryEntries;
	int selectedFileIndex;
	int fileExporerScrollOffset;

	Editor();

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