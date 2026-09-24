#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <stdint.h>
#include <stdio.h>

#pragma comment(lib, "Comctl32.lib")

// ------------------ KEY ------------------
static const uint8_t KEY[16] = {
    0x3F,0x1E,0xC1,0x93,0x26,0x42,0x58,0xB8,
    0xAA,0x9D,0x53,0x75,0xBF,0x62,0x9A,0x97
};

// ------------------ GLOBALS ------------------
HWND hProgress, hStatus, hList, hStartBtn, hClearBtn;
HANDLE hWorkerThread = NULL;

int totalFiles = 0;
int processedFiles = 0;
int stopFlag = 0;

// ------------------ DECRYPT ------------------
void decrypt_file(const char* input, const char* output)
{
    FILE* fin = fopen(input, "rb");
    if (!fin) return;

    fseek(fin, 0, SEEK_END);
    long size = ftell(fin);
    fseek(fin, 0, SEEK_SET);

    uint8_t* data = malloc(size);
    if (!data) {
        fclose(fin);
        return;
    }

    fread(data, 1, size, fin);
    fclose(fin);

    long limit = size < 256 ? size : 256;
    for (long i = 0; i < limit; i++)
        data[i] ^= KEY[i % sizeof(KEY)];

    FILE* fout = fopen(output, "wb");
    if (fout) {
        fwrite(data, 1, size, fout);
        fclose(fout);
    }

    free(data);
}

// ------------------ WORKER THREAD ------------------
DWORD WINAPI WorkerThread(LPVOID lpParam)
{
    int count = SendMessage(hList, LB_GETCOUNT, 0, 0);

    for (int i = 0; i < count && !stopFlag; i++)
    {
        char path[MAX_PATH];
        SendMessageA(hList, LB_GETTEXT, i, (LPARAM)path);

        char out[MAX_PATH];
        strcpy(out, path);

        char* dot = strrchr(out, '.');
        if (dot) strcpy(dot, "_dec.mp4");

        decrypt_file(path, out);

        processedFiles++;

        SendMessage(hProgress, PBM_SETPOS, processedFiles, 0);

        char status[256];
        sprintf(status, "Processed %d / %d", processedFiles, totalFiles);
        SetWindowTextA(hStatus, status);
    }

    SetWindowTextA(hStatus, "Done");
    hWorkerThread = NULL;

    MessageBeep(MB_ICONASTERISK);

    MessageBoxA(NULL,
                "All files have been successfully processed.",
                "Complete",
                MB_OK | MB_ICONASTERISK);

    return 0;
}

// ------------------ FILE ENUMERATION ------------------
void add_file_to_list(const char* path)
{
    SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)path);
}

void scan_path(const char* path)
{
    DWORD attr = GetFileAttributesA(path);

    if (attr == INVALID_FILE_ATTRIBUTES)
        return;

    if (attr & FILE_ATTRIBUTE_DIRECTORY)
    {
        char search[MAX_PATH];
        sprintf(search, "%s\\*.mp4", path);

        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA(search, &fd);

        if (hFind != INVALID_HANDLE_VALUE)
        {
            do {
                char full[MAX_PATH];
                sprintf(full, "%s\\%s", path, fd.cFileName);
                add_file_to_list(full);

            } while (FindNextFileA(hFind, &fd));

            FindClose(hFind);
        }
    }
    else
    {
        const char* ext = strrchr(path, '.');
        if (ext && _stricmp(ext, ".mp4") == 0)
            add_file_to_list(path);
    }
}

// ------------------ START PROCESSING ------------------
void start_processing()
{
    if (hWorkerThread != NULL)
        return;

    int count = SendMessage(hList, LB_GETCOUNT, 0, 0);
    if (count <= 0) return;

    totalFiles = count;
    processedFiles = 0;
    stopFlag = 0;

    SendMessage(hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, totalFiles));
    SendMessage(hProgress, PBM_SETPOS, 0, 0);

    SetWindowTextA(hStatus, "Processing...");

    hWorkerThread = CreateThread(NULL, 0, WorkerThread, NULL, 0, NULL);
}

// ------------------ WINDOW PROCEDURE ------------------
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
        {
            hList = CreateWindowExA(
                WS_EX_CLIENTEDGE, "LISTBOX", "",
                WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL,
                20, 20, 360, 200,
                hwnd, NULL, NULL, NULL);

            hStartBtn = CreateWindowA(
                "BUTTON", "Start",
                WS_CHILD | WS_VISIBLE,
                20, 230, 80, 30,
                hwnd, (HMENU)1, NULL, NULL);

            hClearBtn = CreateWindowA(
                "BUTTON", "Clear",
                WS_CHILD | WS_VISIBLE,
                110, 230, 80, 30,
                hwnd, (HMENU)2, NULL, NULL);

            hProgress = CreateWindowExA(0, PROGRESS_CLASSA, "",
                WS_CHILD | WS_VISIBLE,
                20, 270, 360, 20,
                hwnd, NULL, NULL, NULL);

            hStatus = CreateWindowExA(0, "STATIC", "Idle",
                WS_CHILD | WS_VISIBLE,
                20, 300, 360, 20,
                hwnd, NULL, NULL, NULL);

            DragAcceptFiles(hwnd, TRUE);
        }
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == 1) start_processing();
        if (LOWORD(wParam) == 2) SendMessage(hList, LB_RESETCONTENT, 0, 0);
        break;

    case WM_DROPFILES:
        {
            HDROP hDrop = (HDROP)wParam;

            UINT count = DragQueryFileA(hDrop, 0xFFFFFFFF, NULL, 0);

            for (UINT i = 0; i < count; i++)
            {
                char path[MAX_PATH];
                DragQueryFileA(hDrop, i, path, MAX_PATH);
                scan_path(path);
            }

            DragFinish(hDrop);
        }
        break;

    case WM_DESTROY:
        stopFlag = 1;
        if (hWorkerThread)
            WaitForSingleObject(hWorkerThread, 2000);
        PostQuitMessage(0);
        break;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ------------------ WINMAIN ------------------
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
    InitCommonControls();

    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = "KH_DEC_WIN32";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    RegisterClassA(&wc);

    HWND hwnd = CreateWindowA(
        "KH_DEC_WIN32",
        "KH MP4 Decryptor",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        420, 360,
        NULL, NULL, hInst, NULL);

    ShowWindow(hwnd, nShow);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
