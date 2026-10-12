// player: loop a system WAV for N seconds (default 60). Built under several names so the
// spike has distinct exes to mute/solo.  cl /nologo /DUNICODE player.cpp winmm.lib
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <cstdlib>
#pragma comment(lib, "winmm.lib")
int wmain(int argc, wchar_t** argv) {
    int secs = argc > 1 ? _wtoi(argv[1]) : 60;
    PlaySoundW(L"C:\\Windows\\Media\\Alarm01.wav", nullptr, SND_FILENAME | SND_LOOP | SND_ASYNC);
    Sleep(secs * 1000);
    PlaySoundW(nullptr, nullptr, 0);
    return 0;
}
