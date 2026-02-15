package com.bgmdwldr.vulkan;

import android.app.NativeActivity;
import android.os.Bundle;
import android.util.Log;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.inputmethod.InputMethodManager;

/**
 * MainActivity that forwards all input events to native C layer.
 * The native layer handles all text input and UI interaction.
 */
public class MainActivity extends NativeActivity {
    private static final String TAG = "MainActivity";

    // Native methods for event forwarding
    private static native void nativeOnTouch(int action, float x, float y);
    private static native void nativeOnKey(int keyCode, int unicodeChar, int action);
    
    // Native methods for keyboard control
    private static native void nativeOnKeyboardShown();
    private static native void nativeOnKeyboardHidden();

    static {
        System.loadLibrary("minimalvulkan");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Log.i(TAG, "onCreate: Event forwarding initialized");
    }

    /**
     * Capture all touch events and forward to native layer.
     * Native layer decides what to do based on touch coordinates.
     */
    @Override
    public boolean dispatchTouchEvent(MotionEvent event) {
        int action = event.getActionMasked();
        float x = event.getX();
        float y = event.getY();
        
        // Forward to native layer
        try {
            nativeOnTouch(action, x, y);
        } catch (Exception e) {
            Log.e(TAG, "Error forwarding touch event", e);
        }
        
        // Always consume touch events - native layer handles everything
        return true;
    }

    /**
     * Capture all key events and forward to native layer.
     * Includes soft keyboard input, hardware keys, etc.
     */
    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        int action = event.getAction();
        int keyCode = event.getKeyCode();
        int unicodeChar = event.getUnicodeChar();
        
        Log.i(TAG, "dispatchKeyEvent: action=" + action + 
              " keyCode=" + keyCode + " unicode=" + unicodeChar);
        
        // Forward to native layer
        try {
            nativeOnKey(keyCode, unicodeChar, action);
            Log.i(TAG, "nativeOnKey called successfully");
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "nativeOnKey JNI method not found!", e);
        } catch (Exception e) {
            Log.e(TAG, "Error forwarding key event", e);
        }
        
        // Consume event - native layer handles everything
        return true;
    }

    /**
     * Called by native layer to show the soft keyboard.
     */
    public void showKeyboard() {
        runOnUiThread(() -> {
            InputMethodManager imm = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
            if (imm != null) {
                // Show keyboard for the native content view
                imm.showSoftInput(getWindow().getDecorView(), InputMethodManager.SHOW_IMPLICIT);
                nativeOnKeyboardShown();
                Log.d(TAG, "Keyboard shown");
            }
        });
    }

    /**
     * Called by native layer to hide the soft keyboard.
     */
    public void hideKeyboard() {
        runOnUiThread(() -> {
            InputMethodManager imm = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
            if (imm != null) {
                imm.hideSoftInputFromWindow(getWindow().getDecorView().getWindowToken(), 0);
                nativeOnKeyboardHidden();
                Log.d(TAG, "Keyboard hidden");
            }
        });
    }

    @Override
    public void onBackPressed() {
        // Let native layer handle back button
        // For now, just finish the activity
        finish();
    }
}
