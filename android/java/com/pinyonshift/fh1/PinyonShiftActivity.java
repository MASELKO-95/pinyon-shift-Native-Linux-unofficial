package com.pinyonshift.fh1;

import android.content.Intent;
import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;
import android.util.Log;

import java.io.File;

import org.libsdl.app.SDLActivity;

/**
 * SDL's activity with the game's paths and arguments (AP-1.4, AP-3.3).
 *
 * The game reads its folders from the environment, as tools/pinyon.py sets
 * them on a PC: PINYON_SHIFT_GAME_ROOT holds the files extracted from the
 * player's disc and PINYON_SHIFT_STATE_ROOT the saves, settings, caches, logs
 * and mods. Both live in the app's external files folder, which needs no
 * permission and is reachable over adb and MTP, so a save copies between the
 * device and a PC as a folder. Process environment set here, before SDL
 * starts the native thread, is what the game's getenv sees.
 *
 * Tooling can override any variable with a string extra named env.NAME and
 * pass game arguments as a string-array extra named args:
 *
 *   adb shell am start -n com.pinyonshift.fh1/.PinyonShiftActivity \
 *       --es env.PINYON_SHIFT_FH1_RENDER_TEST_SCRIPT /sdcard/.../route.fh1test \
 *       --esa args --gpu_backend=null
 */
public class PinyonShiftActivity extends SDLActivity {
    private static final String TAG = "PinyonShift";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        File external = getExternalFilesDir(null);
        File base = external != null ? external : getFilesDir();
        setDefaultEnvironment("HOME", getFilesDir().getAbsolutePath());
        setDefaultEnvironment("PINYON_SHIFT_STATE_ROOT",
                new File(base, "state").getAbsolutePath());
        setDefaultEnvironment("PINYON_SHIFT_GAME_ROOT",
                new File(base, "game/base").getAbsolutePath());
        Intent intent = getIntent();
        Bundle extras = intent != null ? intent.getExtras() : null;
        if (extras != null) {
            for (String key : extras.keySet()) {
                if (key.startsWith("env.")) {
                    Object value = extras.get(key);
                    if (value != null) {
                        setEnvironment(key.substring(4), value.toString());
                    }
                }
            }
        }
        super.onCreate(savedInstanceState);
    }

    // The C++ runtime first, then the ReXGlue runtime, which holds SDL and
    // registers its Java natives when loaded, then the game (libmain.so),
    // whose SDL_main SDL calls.
    @Override
    protected String[] getLibraries() {
        return new String[] {"c++_shared", "rexruntime", "main"};
    }

    @Override
    protected String[] getArguments() {
        Intent intent = getIntent();
        String[] arguments = intent != null ? intent.getStringArrayExtra("args") : null;
        return arguments != null ? arguments : new String[0];
    }

    private static void setDefaultEnvironment(String name, String value) {
        if (Os.getenv(name) == null) {
            setEnvironment(name, value);
        }
    }

    private static void setEnvironment(String name, String value) {
        try {
            Os.setenv(name, value, true);
        } catch (ErrnoException error) {
            Log.e(TAG, "setenv " + name + " failed", error);
        }
    }
}
