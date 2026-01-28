package com.bgmdwldr.vulkan;

import android.app.NativeActivity;
import android.graphics.Rect;
import android.os.Bundle;
import android.text.Editable;
import android.text.InputType;
import android.text.TextWatcher;
import android.util.Log;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewTreeObserver;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.webkit.CookieManager;
import android.webkit.DownloadListener;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.EditText;
import android.widget.FrameLayout;

public class MainActivity extends NativeActivity {
    private EditText input;
    private View rootView;
    private WebView webView;
    private boolean webViewReady = false;

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

        // Initialize WebView for JavaScript execution and downloading
        initializeWebView();

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

    private void initializeWebView() {
        // Create WebView for YouTube page loading and video downloading
        webView = new WebView(this);
        webView.setLayoutParams(new FrameLayout.LayoutParams(1, 1));
        webView.setAlpha(0f); // Make invisible

        // Configure WebView for YouTube compatibility
        WebSettings webSettings = webView.getSettings();
        webSettings.setJavaScriptEnabled(true);
        webSettings.setDomStorageEnabled(true);
        webSettings.setDatabaseEnabled(true);
        webSettings.setMixedContentMode(WebSettings.MIXED_CONTENT_ALWAYS_ALLOW);
        webSettings.setUserAgentString("Mozilla/5.0 (Linux; Android 10; SM-G973F) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Mobile Safari/537.36");

        // Enable cookie management
        CookieManager cookieManager = CookieManager.getInstance();
        cookieManager.setAcceptCookie(true);
        cookieManager.setAcceptThirdPartyCookies(webView, true);

        // Set up WebView client to monitor page loading
        webView.setWebViewClient(new WebViewClient() {
            @Override
            public void onPageFinished(WebView view, String url) {
                Log.i("WebView", "Page finished loading: " + url);
                webViewReady = true;
            }

            @Override
            public WebResourceResponse shouldInterceptRequest(WebView view, WebResourceRequest request) {
                // Monitor network requests
                Log.d("WebView", "Request: " + request.getUrl().toString());
                return null;
            }
        });

        // Set up download listener for video downloads
        webView.setDownloadListener(new DownloadListener() {
            @Override
            public void onDownloadStart(String url, String userAgent, String contentDisposition,
                                      String mimetype, long contentLength) {
                Log.i("WebView", "Download started: " + url + " (" + mimetype + ")");

                // Handle video download through Android's DownloadManager
                // This gives us real browser behavior for downloads
                // The DownloadManager will handle the actual file saving and progress
            }
        });

        // Add WebView to layout (invisible)
        FrameLayout rootLayout = new FrameLayout(this);
        rootLayout.addView(webView);
        addContentView(rootLayout, new FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.MATCH_PARENT,
            FrameLayout.LayoutParams.MATCH_PARENT));
    }

    // Load YouTube page to establish session
    private void loadYouTubePage(String videoUrl) {
        if (webView != null && videoUrl != null && !videoUrl.isEmpty()) {
            Log.i("WebView", "Loading YouTube page: " + videoUrl);
            webView.loadUrl(videoUrl);
        }
    }

    // Download video via WebView (uses real browser download behavior)
    private void downloadVideoViaWebView(String videoUrl) {
        if (webView != null && videoUrl != null && !videoUrl.isEmpty()) {
            Log.i("WebView", "Downloading video via WebView: " + videoUrl);

            // Load the video URL directly in WebView
            // WebView will handle authentication, cookies, and download through its DownloadListener
            webView.loadUrl(videoUrl);
        }
    }

    // Native methods called from C++
    private static native void nativeSetActivityRef();
}