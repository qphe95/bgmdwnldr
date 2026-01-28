package com.bgmdwldr.vulkan;

import android.app.NativeActivity;
import android.graphics.Rect;
import android.os.Bundle;
import android.text.Editable;
import android.text.InputType;
import android.text.TextWatcher;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewTreeObserver;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.widget.FrameLayout;

public class MainActivity extends NativeActivity {
    private EditText input;
    private View rootView;

    private static native void nativeOnTextChanged(String text);
    private static native void nativeOnSubmit();
    private static native void nativeOnFocus(boolean focused);
    private static native void nativeOnKeyboardHeight(int heightPx);

    static {
        System.loadLibrary("minimalvulkan");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        input = new EditText(this);
        input.setSingleLine(true);
        input.setImeOptions(EditorInfo.IME_ACTION_DONE);
        input.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
        input.setTextIsSelectable(true);
        input.setLongClickable(true);
        input.setLayoutParams(new FrameLayout.LayoutParams(1, 1));
        input.setAlpha(0f);
        addContentView(input, new FrameLayout.LayoutParams(1, 1));

        rootView = getWindow().getDecorView().getRootView();
        rootView.getViewTreeObserver().addOnGlobalLayoutListener(
            new ViewTreeObserver.OnGlobalLayoutListener() {
                @Override
                public void onGlobalLayout() {
                    Rect r = new Rect();
                    rootView.getWindowVisibleDisplayFrame(r);
                    int screenHeight = rootView.getHeight();
                    int keyboardHeight = Math.max(0, screenHeight - r.bottom);
                    nativeOnKeyboardHeight(keyboardHeight);
                    if (keyboardHeight == 0) {
                        nativeOnFocus(false);
                    }
                }
            }
        );

        input.addTextChangedListener(new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence s, int start, int count, int after) {}

            @Override
            public void onTextChanged(CharSequence s, int start, int before, int count) {
                nativeOnTextChanged(s.toString());
            }

            @Override
            public void afterTextChanged(Editable s) {}
        });

        input.setOnEditorActionListener((v, actionId, event) -> {
            if (actionId == EditorInfo.IME_ACTION_DONE ||
                (event != null && event.getKeyCode() == KeyEvent.KEYCODE_ENTER)) {
                hideKeyboard();
                nativeOnSubmit();
                return true;
            }
            return false;
        });
    }

    public void showKeyboard(boolean clearText) {
        if (input == null) {
            return;
        }
        runOnUiThread(() -> {
            if (clearText) {
                input.setText("");
            } else {
                input.selectAll();
            }
            input.requestFocus();
            nativeOnFocus(true);
            InputMethodManager imm = (InputMethodManager)getSystemService(INPUT_METHOD_SERVICE);
            if (imm != null) {
                imm.showSoftInput(input, InputMethodManager.SHOW_IMPLICIT);
            }
        });
    }

    public void hideKeyboard() {
        if (input == null) {
            return;
        }
        runOnUiThread(() -> {
            InputMethodManager imm = (InputMethodManager)getSystemService(INPUT_METHOD_SERVICE);
            if (imm != null) {
                imm.hideSoftInputFromWindow(input.getWindowToken(), 0);
            }
            input.clearFocus();
            nativeOnFocus(false);
        });
    }
}
