"use strict";

// Gastinel Write — v0 : éditeur de texte brut. JS vanilla, sans framework.
// Le système d'extensions (JS/TS dans la WebView) viendra après la v0.

const { invoke } = window.__TAURI__.core;
const { listen } = window.__TAURI__.event;
const appWindow = window.__TAURI__.window.getCurrentWindow();

const editor = document.getElementById("editor");
const status = document.getElementById("status");

const doc = {
  path: null,
  savedText: "",
  dirty: false,
};

function baseName(path) {
  return path.split(/[\\/]/).pop();
}

function refreshTitle() {
  const name = doc.path ? baseName(doc.path) : "Sans titre";
  appWindow.setTitle(`${doc.dirty ? "• " : ""}${name} — Gastinel Write`);
}

function setDirty(dirty) {
  if (dirty === doc.dirty) return;
  doc.dirty = dirty;
  invoke("set_dirty", { dirty });
  refreshTitle();
}

// Compter les mots sur chaque frappe coûte cher sur de gros documents :
// on regroupe le calcul sur la prochaine frame d'inactivité.
let statusPending = false;
function scheduleStatus() {
  if (statusPending) return;
  statusPending = true;
  const run = () => {
    statusPending = false;
    const text = editor.value;
    const words = text.match(/[^\s]+/g)?.length ?? 0;
    status.textContent =
      `${words} mot${words > 1 ? "s" : ""} · ${text.length} caractère${text.length > 1 ? "s" : ""}`;
  };
  (window.requestIdleCallback ?? requestAnimationFrame)(run);
}

function load(path, contents) {
  doc.path = path;
  doc.savedText = contents;
  editor.value = contents;
  editor.setSelectionRange(0, 0);
  editor.scrollTop = 0;
  setDirty(false);
  refreshTitle();
  scheduleStatus();
  editor.focus();
}

async function report(err) {
  console.error(err);
  status.textContent = String(err);
}

async function save({ as = false } = {}) {
  try {
    const path = await invoke("save_file", {
      path: as ? null : doc.path,
      contents: editor.value,
    });
    if (!path) return false;
    doc.path = path;
    doc.savedText = editor.value;
    setDirty(false);
    refreshTitle();
    return true;
  } catch (err) {
    await report(err);
    return false;
  }
}

// Renvoie false si l'utilisateur annule.
async function confirmDiscard() {
  if (!doc.dirty) return true;
  const choice = await invoke("ask_unsaved");
  if (choice === "cancel") return false;
  if (choice === "save") return save();
  return true;
}

const actions = {
  async new() {
    if (await confirmDiscard()) load(null, "");
  },
  async open() {
    if (!(await confirmDiscard())) return;
    try {
      const file = await invoke("open_file");
      if (file) load(file.path, file.contents);
    } catch (err) {
      await report(err);
    }
  },
  save: () => save(),
  saveAs: () => save({ as: true }),
};

editor.addEventListener("input", () => {
  // Comparer tout le texte à chaque frappe serait O(n) : on ne le fait
  // que lorsque la longueur coïncide, cas où l'on a pu revenir à l'état enregistré.
  const back = editor.value.length === doc.savedText.length && editor.value === doc.savedText;
  setDirty(!back);
  scheduleStatus();
});

editor.addEventListener("keydown", (e) => {
  if (e.key === "Tab" && !e.ctrlKey && !e.altKey && !e.metaKey) {
    e.preventDefault();
    // execCommand conserve l'historique d'annulation natif, contrairement à une affectation de .value.
    document.execCommand("insertText", false, "\t");
  }
});

document.addEventListener("keydown", (e) => {
  if (!(e.ctrlKey || e.metaKey) || e.altKey) return;
  const key = e.key.toLowerCase();
  const action =
    key === "n" ? "new" :
    key === "o" ? "open" :
    key === "s" ? (e.shiftKey ? "saveAs" : "save") :
    null;
  if (action) {
    e.preventDefault();
    actions[action]();
  }
});

for (const button of document.querySelectorAll("[data-action]")) {
  button.addEventListener("click", () => {
    actions[button.dataset.action]();
    editor.focus();
  });
}

// Fermeture demandée alors que le document est modifié (interceptée côté Rust).
listen("gw://close-requested", async () => {
  if (await confirmDiscard()) invoke("quit");
});

(async () => {
  refreshTitle();
  scheduleStatus();
  const path = await invoke("startup_path");
  if (path) {
    try {
      const file = await invoke("open_path", { path });
      load(file.path, file.contents);
    } catch (err) {
      await report(err);
    }
  }
})();
