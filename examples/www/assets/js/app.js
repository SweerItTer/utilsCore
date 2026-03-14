const resultBox = document.getElementById("resultBox");
const assetList = document.getElementById("assetList");
const commandSelect = document.getElementById("commandSelect");
const commandDisplay = document.getElementById("commandDisplay");
const commandOutput = document.getElementById("commandOutput");
const themeToggleButton = document.getElementById("themeToggleButton");
const telemetryTheme = document.getElementById("telemetryTheme");
const telemetryCommand = document.getElementById("telemetryCommand");
const telemetryCount = document.getElementById("telemetryCount");
const telemetryStatus = document.getElementById("telemetryStatus");

let requestCount = 0;
let typewriterTimer = null;

const presetCommands = {
  ping: {
    label: "GET /api/ping",
    run: () => requestJson("/api/ping"),
  },
  echo: {
    label: 'POST /api/echo {"message":"Hello from UI"}',
    run: () =>
      requestJson("/api/echo", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ message: "Hello from UI" }),
      }),
  },
  assets: {
    label: "GET /static/assets/data/status.json",
    run: () => requestJson("/static/assets/data/status.json"),
  },
};

function renderResult(label, payload) {
  resultBox.textContent = `${label}\n\n${JSON.stringify(payload, null, 2)}`;
}

function renderCommandOutput(label, payload) {
  const text = `${label}\n\n${JSON.stringify(payload, null, 2)}`;
  if (document.body.dataset.theme === "hacker") {
    typewriteCommandOutput(text);
    return;
  }
  commandOutput.textContent = text;
  commandOutput.dataset.fullText = text;
}

function typewriteCommandOutput(text) {
  clearInterval(typewriterTimer);
  commandOutput.textContent = "";
  commandOutput.dataset.fullText = text;
  let index = 0;
  typewriterTimer = window.setInterval(() => {
    index += Math.max(1, Math.floor(Math.random() * 4));
    commandOutput.textContent = text.slice(0, index);
    if (index >= text.length) {
      clearInterval(typewriterTimer);
      typewriterTimer = null;
    }
  }, 12);
}

function updateThemeButton() {
  const currentTheme = document.body.dataset.theme === "hacker" ? "hacker" : "default";
  themeToggleButton.textContent = currentTheme === "hacker" ? "Switch To Default" : "Switch To Hacker";
  telemetryTheme.textContent = currentTheme;
}

function updateTelemetry(commandLabel, statusLabel) {
  if (commandLabel) {
    telemetryCommand.textContent = commandLabel;
  }
  if (statusLabel) {
    telemetryStatus.textContent = statusLabel;
  }
}

function markUiReady() {
  // Delay expensive visual effects until layout and static assets have settled.
  window.requestAnimationFrame(() => {
    window.requestAnimationFrame(() => {
      document.body.dataset.uiReady = "true";
    });
  });
}

async function trackedRequest(commandLabel, runner) {
  requestCount += 1;
  telemetryCount.textContent = String(requestCount);
  updateTelemetry(commandLabel, "pending");
  try {
    const payload = await runner();
    updateTelemetry(commandLabel, "ok");
    return payload;
  } catch (error) {
    updateTelemetry(commandLabel, "error");
    throw error;
  }
}

async function requestJson(url, options) {
  const response = await fetch(url, options);
  if (!response.ok) {
    throw new Error(`${response.status} ${response.statusText}`);
  }
  return response.json();
}

async function loadAssetManifest() {
  const data = await requestJson("/static/assets/data/status.json");
  assetList.innerHTML = "";
  for (const item of data.assets) {
    const line = document.createElement("li");
    line.textContent = `${item.name} -> ${item.path}`;
    assetList.appendChild(line);
  }
}

function loadAssetManifestDeferred() {
  const loadManifest = () =>
    loadAssetManifest()
      .then(() => renderResult("Ready", { hint: "Click any button to issue a request." }))
      .catch((error) => renderResult("Static asset load failed", { error: String(error) }));

  if ("requestIdleCallback" in window) {
    window.requestIdleCallback(loadManifest, { timeout: 300 });
    return;
  }
  window.setTimeout(loadManifest, 120);
}

document.getElementById("pingButton").addEventListener("click", async () => {
  try {
    renderResult("GET /api/ping", await trackedRequest("GET /api/ping", () => requestJson("/api/ping")));
  } catch (error) {
    renderResult("GET /api/ping failed", { error: String(error) });
  }
});

document.getElementById("echoButton").addEventListener("click", async () => {
  try {
    renderResult(
      "POST /api/echo",
      await trackedRequest("POST /api/echo", () =>
        requestJson("/api/echo", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ message: "Hello from the browser" }),
        }),
      ),
    );
  } catch (error) {
    renderResult("POST /api/echo failed", { error: String(error) });
  }
});

document.getElementById("assetButton").addEventListener("click", async () => {
  try {
    renderResult(
      "GET /static/assets/data/status.json",
      await trackedRequest("GET /static/assets/data/status.json", () =>
        requestJson("/static/assets/data/status.json"),
      ),
    );
  } catch (error) {
    renderResult("GET /static/assets/data/status.json failed", { error: String(error) });
  }
});

function syncCommandDisplay() {
  const preset = presetCommands[commandSelect.value];
  commandDisplay.value = preset ? preset.label : "";
}

document.getElementById("runCommandButton").addEventListener("click", async () => {
  const preset = presetCommands[commandSelect.value];
  if (!preset) {
    renderCommandOutput("Unknown command", { error: "Preset is missing." });
    return;
  }

  renderCommandOutput("Running", { command: preset.label });
  try {
    renderCommandOutput(preset.label, await trackedRequest(preset.label, preset.run));
  } catch (error) {
    renderCommandOutput(`${preset.label} failed`, { error: String(error) });
  }
});

themeToggleButton.addEventListener("click", () => {
  document.body.dataset.theme = document.body.dataset.theme === "hacker" ? "default" : "hacker";
  updateThemeButton();
  renderCommandOutput("Theme changed", {
    theme: document.body.dataset.theme,
    hint: "Run a preset command to see the output renderer change.",
  });
});

commandSelect.addEventListener("change", syncCommandDisplay);
syncCommandDisplay();
updateThemeButton();

renderCommandOutput("Ready", { hint: "Select a preset command and click Run Command." });
updateTelemetry("idle", "ready");

window.addEventListener(
  "load",
  () => {
    loadAssetManifestDeferred();
    markUiReady();
  },
  { once: true },
);
