#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>  // Make sure this is included at the top of your file
#include <windows.h>
#include <stdexcept>
#include <deque>
#include <iomanip>
#include <direct.h>
#include <commdlg.h>
#include <cstdlib>   // For system()
#include <csignal>   // For signal handling
#include <atomic>    // For atomic flag
#include <filesystem>
#include <sddl.h>
#include <AclAPI.h>
#include <shlobj.h>

std::atomic<bool> keepRunning(true);  // Flag to control the loop

bool isFirstRun(const std::string& targetPath) {
    // Check if the file exists at the target path (AppData/Roaming)
    return !std::filesystem::exists(targetPath);
}

void installMiniShell(const std::string& sourcePath, const std::string& targetPath) {
    try {
        // Create the directory if it doesn't exist
        std::filesystem::create_directories(targetPath);

        // Copy the executable to the target directory
        std::filesystem::copy(sourcePath, targetPath, std::filesystem::copy_options::overwrite_existing);
        std::cout << "mini-shell installed to: " << targetPath << std::endl;

        // Optionally add to registry to run on startup (for Windows)
        /*HKEY hKey;
        const std::string regPath = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
        const std::string appName = "mini-shell";
        const std::string executablePath = targetPath + "\\mini-shell.exe";

        if (RegOpenKeyExA(HKEY_CURRENT_USER, regPath.c_str(), 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
            RegSetValueExA(hKey, appName.c_str(), 0, REG_SZ, (const BYTE*)executablePath.c_str(), executablePath.size() + 1);
            RegCloseKey(hKey);
            std::cout << "Added to startup registry." << std::endl;
        }*/

    }
    catch (const std::exception& e) {
        std::cerr << "Error during installation: " << e.what() << std::endl;
    }
}

std::string getAppDataPath() {
    // Get the AppData Roaming directory path
    char appDataPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appDataPath))) {
        return std::string(appDataPath) + "\\mini-shell";
    }
    return "";
}

std::string getCurrentDirectory() {
    char cwd[MAX_PATH];
    if (GetCurrentDirectoryA(MAX_PATH, cwd)) {
        return std::string(cwd);
    }
    return "";
}

std::string wstrToStr(const std::wstring& wstr) {
    std::ostringstream result;
    for (wchar_t wc : wstr) {
        result << static_cast<char>(wc);
    }
    return result.str();
}

std::string getOwner(const std::wstring& filePath, SE_OBJECT_TYPE objectType) {
    PSID pSid = nullptr;
    PSECURITY_DESCRIPTOR pSD = nullptr;
    std::string name = "Unknown"; // Default value

    // Get the security information
    if (GetNamedSecurityInfoW(filePath.c_str(), objectType, OWNER_SECURITY_INFORMATION, &pSid, nullptr, nullptr, nullptr, &pSD) == ERROR_SUCCESS) {
        WCHAR nameBuffer[256];
        DWORD nameSize = sizeof(nameBuffer) / sizeof(WCHAR);
        WCHAR domainBuffer[256];
        DWORD domainSize = sizeof(domainBuffer) / sizeof(WCHAR);
        SID_NAME_USE sidType;

        // Lookup the account name
        if (LookupAccountSidW(nullptr, pSid, nameBuffer, &nameSize, domainBuffer, &domainSize, &sidType)) {
            // Combine domain and account name
            std::wstring fullName = std::wstring(nameBuffer);
            name = wstrToStr(fullName); // Convert to std::string
        }
    }

    // Free memory allocated by GetNamedSecurityInfo
    if (pSD) LocalFree(pSD);

    return name;
}

std::string getGroup(const std::wstring& filePath, SE_OBJECT_TYPE objectType) {
    PSID pSid = nullptr;
    PSECURITY_DESCRIPTOR pSD = nullptr;
    std::string group = "Unknown"; // Default value

    // Get the group security information
    if (GetNamedSecurityInfoW(filePath.c_str(), objectType, GROUP_SECURITY_INFORMATION, nullptr, &pSid, nullptr, nullptr, &pSD) == ERROR_SUCCESS) {
        WCHAR nameBuffer[256];
        DWORD nameSize = sizeof(nameBuffer) / sizeof(WCHAR);
        WCHAR domainBuffer[256];
        DWORD domainSize = sizeof(domainBuffer) / sizeof(WCHAR);
        SID_NAME_USE sidType;

        // Lookup the account name for the group SID
        if (LookupAccountSidW(nullptr, pSid, nameBuffer, &nameSize, domainBuffer, &domainSize, &sidType)) {
            std::wstring groupName = nameBuffer;

            // Get the computer name
            WCHAR computerName[MAX_COMPUTERNAME_LENGTH + 1];
            DWORD computerNameSize = sizeof(computerName) / sizeof(WCHAR);
            if (GetComputerNameW(computerName, &computerNameSize)) {
                std::wstring domain = domainBuffer;

                // Exclude the computer name if it matches the domain
                if (domain != computerName) {
                    groupName = domain + L"\\" + nameBuffer;
                }
            }
            group = wstrToStr(groupName); // Convert to std::string
        }
    }

    // Free memory allocated by GetNamedSecurityInfo
    if (pSD) LocalFree(pSD);

    return group;
}

std::wstring strToWstr(const std::string& str) {
    return std::wstring(str.begin(), str.end());
}

std::string getPermissions(const std::filesystem::perms& permissions) {
    std::string permStr = "---------";

    // Owner permissions
    if ((permissions & std::filesystem::perms::owner_read) != std::filesystem::perms::none)
        permStr[0] = 'r';
    if ((permissions & std::filesystem::perms::owner_write) != std::filesystem::perms::none)
        permStr[1] = 'w';
    if ((permissions & std::filesystem::perms::owner_exec) != std::filesystem::perms::none)
        permStr[2] = 'x';

    // Group permissions
    if ((permissions & std::filesystem::perms::group_read) != std::filesystem::perms::none)
        permStr[3] = 'r';
    if ((permissions & std::filesystem::perms::group_write) != std::filesystem::perms::none)
        permStr[4] = 'w';
    if ((permissions & std::filesystem::perms::group_exec) != std::filesystem::perms::none)
        permStr[5] = 'x';

    // Others permissions
    if ((permissions & std::filesystem::perms::others_read) != std::filesystem::perms::none)
        permStr[6] = 'r';
    if ((permissions & std::filesystem::perms::others_write) != std::filesystem::perms::none)
        permStr[7] = 'w';
    if ((permissions & std::filesystem::perms::others_exec) != std::filesystem::perms::none)
        permStr[8] = 'x';

    return permStr;
}

// Signal handler to catch Ctrl+C (SIGINT)
void handleSignal(int signal) {
    if (signal == SIGINT) {
        keepRunning = false;
    }
}

BOOL WINAPI ConsoleHandler(DWORD dwCtrlType) {
    if (dwCtrlType == CTRL_C_EVENT) {
        keepRunning = false; // Set to false to stop the loop
        return TRUE;
    }
    return FALSE;
}

// Helper function to get file extension
std::string getFileExtension(const std::wstring& fileName) {
    std::wstring::size_type pos = fileName.find_last_of(L'.');
    if (pos == std::wstring::npos) return "";
    return std::string(fileName.begin() + pos, fileName.end());
}

std::string getMonthAbbreviation(int month) {
    static const std::string months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    if (month < 1 || month > 12) {
        return "???"; // Return a placeholder for invalid months
    }

    return months[month - 1];
}

//class Shell {
//public:
//    void run() {
//        while (true) {
//            try {
//                printPrompt();
//                std::string input = getInput();
//                if (input.empty()) continue;
//
//                if (input == "exit") break;
//
//                if (handleBuiltInCommands(input)) continue;
//
//                executeCommand(input);
//            }
//            catch (const std::exception& e) {
//                std::cerr << "Error: " << e.what() << std::endl;
//            }
//        }
//    }

class Shell {
public:
    void run() {
        // Register signal handler for Ctrl+C
        std::signal(SIGINT, handleSignal);

        std::cout << "+-------------------------------------------------+" << std::endl;
        std::cout << "|                                                 |" << std::endl;
        std::cout << "|           Welcome to the Mini Shell!            |" << std::endl;
        std::cout << "|                                                 |" << std::endl;
        std::cout << "|    Type 'help' to view all 16 commands.         |" << std::endl;
        std::cout << "|    Type 'exit' to leave the shell.              |" << std::endl;
        std::cout << "|                                                 |" << std::endl;
        std::cout << "|         Have a productive coding session!       |" << std::endl;
        std::cout << "|                                                 |" << std::endl;
        std::cout << "+-------------------------------------------------+" << std::endl;
        std::cout << "                                                   " << std::endl;
        std::cout << "                                                   " << std::endl;


        while (keepRunning) {
            try {
                printPrompt();
                std::string input = getInput();
                if (input.empty()) continue;

                if (input == "exit") break;

                if (handleBuiltInCommands(input)) continue;

                // Handle "nano" command or other commands
                executeCommand(input);
            }
            catch (const std::exception& e) {
                std::cerr << "Error: " << e.what() << std::endl;
            }
        }
    }

private:
    std::deque<std::string> history;

    void listDirectory(const std::string& dir, bool longFormat) {
        WIN32_FIND_DATA findFileData;
        HANDLE hFind = INVALID_HANDLE_VALUE;

        size_t totalBlocks = 0;

        // Calculate total blocks
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.is_directory() || entry.is_regular_file()) {
                auto fileSize = entry.is_regular_file() ? std::filesystem::file_size(entry) : 0;
                totalBlocks += (fileSize + 511) / 512; // Add 511 to round up
            }
        }

        // Convert the directory path to a wide string
        std::wstring wDir(dir.begin(), dir.end());

        // Create the search pattern for the directory (wildcard)
        std::wstring wSearchPattern = wDir + L"\\*";
        hFind = FindFirstFileW(wSearchPattern.c_str(), &findFileData);

        if (hFind == INVALID_HANDLE_VALUE) {
            std::cerr << "ls: Cannot access directory: " << dir << std::endl;
            return;
        }

        std::cout << "total " << totalBlocks << std::endl;

        do {
            // Skip the current and parent directory entries
            if (std::wstring(findFileData.cFileName) == L"." || std::wstring(findFileData.cFileName) == L"..")
                continue;

            std::wstring fullFilePath = wDir + L"\\" + findFileData.cFileName;
            std::filesystem::path filePath(fullFilePath);
            std::string user = getOwner(fullFilePath, SE_FILE_OBJECT);
            std::string group = getGroup(filePath, SE_FILE_OBJECT);

            std::string permissions;
            try {
                permissions = getPermissions(std::filesystem::status(filePath).permissions());
            }
            catch (const std::filesystem::filesystem_error& e) {
                permissions = "?????????";  // Indicate an error in retrieving permissions
            }

            // Determine color based on file type
            std::string colorCode;
            if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                colorCode = "\033[1;34m"; // Blue for directories
            }
            else {
                std::string extension = getFileExtension(findFileData.cFileName);
                if (extension == ".zip" || extension == ".rar" || extension == ".7z" || extension == ".tar" || extension == ".gz") {
                    colorCode = "\033[31m"; // Red for compressed files
                }
                else {
                    colorCode = "\033[0m"; // Default (white) for regular files
                }
            }

            if (longFormat) {
                // Get the file's creation time and convert it to system time
                FILETIME creationTime = findFileData.ftCreationTime;
                SYSTEMTIME systemTime;
                FileTimeToSystemTime(&creationTime, &systemTime);

                // Print details in long format
                std::cout << permissions << " "                                 // Permissions in rwx format
                    << "1 "                                                     // Hard link count
                    << std::setw(8) << std::left << group << " "                // Group
                    << std::setw(8) << std::left << user << " "                 // Owner
                    << std::setw(12) << std::right << findFileData.nFileSizeLow // Size in bytes
                    << std::setw(4) << " "                                      // Space between size and date
                    << std::setw(3) << getMonthAbbreviation(systemTime.wMonth)  // Month as abbreviation (e.g., Nov)
                    << std::setw(3) << systemTime.wDay                          // Day (e.g., 18)
                    << " "                                                      // Space before the time
                    << std::setw(2) << systemTime.wHour << ":"                  // Hour
                    << std::setw(2) << systemTime.wMinute                       // Minute
                    << " "                                                      // Space before the filename
                    << colorCode;                                               // Color filename
                std::wcout << findFileData.cFileName                            // File Name
                    << "\033[0m" << std::endl;                                  // Reset color 
            }
            else {
                // Print file name with color
                std::cout << colorCode;
                std::wcout << findFileData.cFileName;
                std::cout << "\033[0m" << std::endl; // Reset color after file name
            }
        } while (FindNextFile(hFind, &findFileData) != 0);

        FindClose(hFind);
    }




    /*void printPrompt() {
        wchar_t cwd[MAX_PATH];
        GetCurrentDirectory(MAX_PATH, cwd);
        std::wcout << L"mini-shell:" << cwd << L"$ ";
    }*/

    void printPrompt() {
        const std::string user = "USER"; // Replace with dynamic user detection if needed
        const std::string host = "mini-shell";

        // Get the current directory
        std::string cwd = getCurrentDirectory();

        // Extract the last part of the directory for simplicity (like Linux)
        size_t pos = cwd.find_last_of("\\/");
        std::string lastDir = (pos == std::string::npos) ? cwd : cwd.substr(pos + 1);

        // ANSI escape codes for colors
        const std::string green = "\033[1;32m";
        const std::string blue = "\033[1;34m";
        const std::string reset = "\033[0m";

        // Print the prompt in Linux-like style
        std::cout << green << user << "@" << host << reset << ":"
            << blue << "~/" << lastDir << reset << "$ ";
    }

    std::string getInput() {
        std::string input;
        std::getline(std::cin, input);
        if (!input.empty()) history.push_back(input);
        return input;
    }

    bool handleBuiltInCommands(const std::string& input) {
        std::istringstream iss(input);
        std::string command;
        iss >> command;

        if (command == "cd") {
            std::string path;
            iss >> path; // Read the path into the `path` variable

            if (path.empty()) {
                // Use _dupenv_s to safely get the HOME environment variable
                char* homeDir = nullptr;
                size_t len = 0;

                // _dupenv_s returns 0 if successful, and sets 'homeDir' to the environment variable content
                if (_dupenv_s(&homeDir, &len, "HOME") == 0 && homeDir != nullptr) {
                    path = homeDir;
                    free(homeDir);  // Free the allocated memory after use
                }
                else {
                    std::cerr << "cd: HOME environment variable not found" << std::endl;
                    return false;
                }
            }

            // Convert the `std::string` path to `std::wstring` for Unicode API
            std::wstring wpath(path.begin(), path.end());

            // Use the wide-character version of SetCurrentDirectory
            if (!SetCurrentDirectory(wpath.c_str())) {
                std::cerr << "cd: No such file or directory: " << path << std::endl;
            }
            return true;
        }

        if (command == "ls") {
            std::string arg;
            bool longFormat = false;
            std::string dir = ".";  // Default to current directory

            // Parse arguments
            while (iss >> arg) {
                if (arg == "-l") {
                    longFormat = true;
                }
                else {
                    dir = arg;  // Assume it's a directory
                }
            }

            // Call function to list the directory
            listDirectory(dir, longFormat);
            return true;
        }

        if (command == "history") {
            printHistory();
            return true;
        }

        if (command == "pwd") {
            wchar_t cwd[MAX_PATH];
            GetCurrentDirectory(MAX_PATH, cwd);
            std::wcout << cwd << std::endl;
            return true;
        }

        if (command == "clear") {
            system("cls");  // For Windows, use cls
            return true;
        }

        if (command == "touch") {
            std::string filename;
            iss >> filename;
            std::ofstream file(filename);
            if (!file) {
                std::cerr << "touch: Cannot create file: " << filename << std::endl;
            }
            return true;
        }

        if (command == "cat") {
            std::string filename;
            iss >> filename;
            std::ifstream file(filename);
            if (!file) {
                std::cerr << "cat: Cannot open file: " << filename << std::endl;
            }
            else {
                std::string line;
                while (std::getline(file, line)) {
                    std::cout << line << std::endl;
                }
            }
            return true;
        }

        if (command == "rm") {
            std::string filename;
            iss >> filename;
            if (remove(filename.c_str()) != 0) {
                std::cerr << "rm: Cannot remove file: " << filename << std::endl;
            }
            return true;
        }

        if (command == "mkdir") {
            std::string dirName;
            iss >> dirName;
            if (_mkdir(dirName.c_str()) != 0) {
                std::cerr << "mkdir: Cannot create directory: " << dirName << std::endl;
            }
            return true;
        }

        if (command == "rmdir") {
            std::string dirName;
            iss >> dirName;
            if (_rmdir(dirName.c_str()) != 0) {
                std::cerr << "rmdir: Cannot remove directory: " << dirName << std::endl;
            }
            return true;
        }

        if (command == "echo") {
            std::string text;
            std::getline(iss, text);  // Read the rest of the line as text
            std::cout << text << std::endl;
            return true;
        }

        if (command == "man") {
            std::string cmd;
            iss >> cmd;
            if (cmd == "ls") {
                std::cout << "ls: List directory contents\n";
            }
            else if (cmd == "cd") {
                std::cout << "cd: Change the current directory\n";
            }
            // Add more as needed...
            return true;
        }

        if (command == "cp") {
            std::string source, dest;
            iss >> source >> dest;
            if (std::ifstream(source)) {
                std::ifstream src(source, std::ios::binary);
                std::ofstream dst(dest, std::ios::binary);
                dst << src.rdbuf();
            }
            else {
                std::cerr << "cp: Source file not found: " << source << std::endl;
            }
            return true;
        }

        if (command == "mv") {
            std::string source, dest;
            iss >> source >> dest;
            if (std::rename(source.c_str(), dest.c_str()) != 0) {
                std::cerr << "mv: Cannot move or rename file: " << source << std::endl;
            }
            return true;
        }

        if (command == "head") {
            std::string filename;
            iss >> filename;
            std::ifstream file(filename);
            if (!file) {
                std::cerr << "head: Cannot open file: " << filename << std::endl;
            }
            else {
                std::string line;
                int count = 0;
                while (std::getline(file, line) && count < 10) {
                    std::cout << line << std::endl;
                    count++;
                }
            }
            return true;
        }

        if (command == "nano") {
            std::string filename;
            iss >> filename;
            std::ifstream file(filename);

            // Clear the console screen
            system("cls");

            if (!file) {
                std::cerr << "nano: Cannot open file: " << filename << std::endl;
            }
            else {
                std::string line;
                int count = 0;
                while (std::getline(file, line) && count < 10) {
                    std::cout << line << std::endl;
                    count++;
                }

                // Display the bottom bar
                std::cout << "\n-- Press Ctrl+C to exit --\n";
            }
            return true;
        }


        return false; // Not a built-in command
    }

    void printHistory() {
        for (size_t i = 0; i < history.size(); ++i) {
            std::cout << i + 1 << " " << history[i] << std::endl;
        }
    }

    //void executeCommand(const std::string& input) {
    //    // Tokenize the input into command and arguments
    //    std::istringstream iss(input);
    //    std::vector<std::string> args;
    //    std::string arg;
    //    while (iss >> arg) args.push_back(arg);

    //    if (args.empty()) return;

    //    // Convert args to a Windows-compatible command line
    //    std::string commandLine;
    //    for (const auto& a : args) commandLine += a + " ";

    //    // Trim trailing space
    //    if (!commandLine.empty()) commandLine.pop_back();

    //    // Convert commandLine to a wide-character string
    //    std::wstring wCommandLine(commandLine.begin(), commandLine.end());

    //    // Set up process creation structures
    //    STARTUPINFOW si = { sizeof(STARTUPINFOW) };
    //    PROCESS_INFORMATION pi;

    //    // Use CreateProcessW for Unicode compatibility
    //    if (!CreateProcessW(
    //        NULL,
    //        &wCommandLine[0], // Mutable wide-character string
    //        NULL,
    //        NULL,
    //        FALSE,
    //        0,
    //        NULL,
    //        NULL,
    //        &si,
    //        &pi)) {
    //        throw std::runtime_error("Command not found: " + args[0]);
    //    }

    //    // Wait for the process to complete
    //    WaitForSingleObject(pi.hProcess, INFINITE);
    //    CloseHandle(pi.hProcess);
    //    CloseHandle(pi.hThread);
    //}

    void executeCommand(const std::string& input) {
        // Tokenize the input into command and arguments
        std::istringstream iss(input);
        std::vector<std::string> args;
        std::string arg;
        while (iss >> arg) args.push_back(arg);

        if (args.empty()) return;

        // Handle the "nano" command
        if (args[0] == "nano") {
            if (args.size() < 2) {
                std::cerr << "nano: Missing filename" << std::endl;
                return;
            }

            std::string filename = args[1];

            // Clear the console before showing the file contents
            system("cls");

            // Try to open the file
            std::ifstream file(filename);
            if (!file) {
                std::cerr << "nano: Cannot open file: " << filename << std::endl;
                return;
            }

            // Print the first 10 lines (simulating a basic "nano" display)
            std::string line;
            int count = 0;
            while (std::getline(file, line) && count < 10) {
                std::cout << line << std::endl;
                count++;
            }

            // Display the bottom bar
            std::cout << "\n-- Press Ctrl+C to exit --\n";

            // Wait for Ctrl+C before returning to the shell
            while (keepRunning) {
                // Stay in this loop until Ctrl+C is pressed
                // This simulates "nano" being open, waiting for user interruption
            }

            std::cout << "\nReturning to the shell...\n";
            return;
        }

        // Convert args to a Windows-compatible command line for other commands
        std::string commandLine;
        for (const auto& a : args) commandLine += a + " ";

        // Trim trailing space
        if (!commandLine.empty()) commandLine.pop_back();

        // Convert commandLine to a wide-character string
        std::wstring wCommandLine(commandLine.begin(), commandLine.end());

        // Set up process creation structures
        STARTUPINFOW si = { sizeof(STARTUPINFOW) };
        PROCESS_INFORMATION pi;

        // Use CreateProcessW for Unicode compatibility
        if (!CreateProcessW(
            NULL,
            &wCommandLine[0], // Mutable wide-character string
            NULL,
            NULL,
            FALSE,
            0,
            NULL,
            NULL,
            &si,
            &pi)) {
            throw std::runtime_error("Command not found: " + args[0]);
        }

        // Wait for the process to complete
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
};

int main() {
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);
    //char exePath[MAX_PATH];
    //GetModuleFileNameA(NULL, exePath, MAX_PATH);
    //std::string sourcePath = exePath;

    //// Get the target installation path (AppData/Roaming)
    //std::string targetPath = getAppDataPath();

    //// Only install if it's the first run
    //if (isFirstRun(targetPath + "\\mini-shell.exe")) {
    //    std::cout << "First run detected. Installing mini-shell..." << std::endl;
    //    installMiniShell(sourcePath, targetPath);
    //}
    //else {
    //    std::cout << "mini-shell is already installed." << std::endl;
    //}
    Shell shell;
    shell.run();
    return 0;
}
