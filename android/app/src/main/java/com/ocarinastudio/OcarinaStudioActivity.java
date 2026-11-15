package com.ocarinastudio;

import org.libsdl.app.SDLActivity;

/** The game itself: SDL creates the window and runs libsoh.so. */
public class OcarinaStudioActivity extends SDLActivity {
    @Override
    protected void onCreate(android.os.Bundle savedInstanceState) {
        DataDir.exportToNative(); // before SDL starts the game
        super.onCreate(savedInstanceState);
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "c++_shared", "SDL2", "soh" };
    }
}
