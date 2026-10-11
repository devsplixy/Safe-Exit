#pragma once
#include <string>

std::string GetDecryptedHTML() {
    return
        "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
        "<title>Safe Exit</title></head>"
        "<body style=\"margin:0;height:100vh;display:grid;place-items:center;"
        "background:radial-gradient(120% 90% at 50% -20%,#15222e 0,#0a1215 46%,#050807 100%);"
        "color:#eef5f1;font-family:system-ui,'Segoe UI',sans-serif;text-align:center\">"
        "<div style=\"padding:24px\">"
        "<h1 style=\"margin:0 0 10px;font-size:18px;letter-spacing:.3em\">SAFE EXIT</h1>"
        "<p style=\"margin:0;max-width:360px;line-height:1.6;color:#8a988f\">"
        "The interface for this build is not part of the public source.<br>"
        "Everything else is here, so you can build your own screen on top of it."
        "</p></div></body></html>";
}
