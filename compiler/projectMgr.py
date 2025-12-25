import json
from pathlib import Path
import threading
import base64
from typing import Optional
from PySide6.QtWidgets import (
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QPushButton,
    QMessageBox,
    QLineEdit,
    QComboBox,
    QApplication,
    QDialog,
    QDialogButtonBox,
    QLabel,
    QFormLayout,
    QSizePolicy,
    QSplitter,
    QPlainTextEdit,
    QStackedWidget,
    QScrollArea,
    QTreeWidget,
    QToolBar,
    QSlider,
    QTreeView,
    QFileSystemModel,
    QTreeWidgetItem,
    QFileDialog,
    QGridLayout,
    QFrame,
    QToolButton,
    QGraphicsDropShadowEffect,
    QGraphicsOpacityEffect,
    QListWidget,
)
from PySide6.QtGui import (
    QSyntaxHighlighter,
    QTextCharFormat,
    QColor,
    QFont,
    QKeySequence,
    QTextCursor,
    QAction,
    QImageReader,
    QPixmap,
    QFontDatabase,
)
from PySide6.QtCore import (
    QSortFilterProxyModel,
    QSettings,
    QDir,
    Signal,
    QObject,
    Qt,
    QRegularExpression,
    QUrl,
    QPropertyAnimation,
    QEasingCurve,
    QEvent,
    QTimer,
)
from PySide6.QtMultimedia import QMediaPlayer
from compile import build_project
import os
import sys
import io
import re
import struct
import shutil
import zipfile, tarfile
import atexit

PREF_PATH = Path.home() / ".vr_engine_compiler/prefs.ini"
PREF_PATH.parent.mkdir(exist_ok=True, parents=True)
PREF_DEFAULTS = {
    "recent_projects": [],
    "selected_project": "",
    "last_project": "",
    "window_size": [1100, 720],
    "project_root": "",  # legacy single root
    "project_root_confirmed": False,
    "search_paths": [],
}


def _load_prefs():
    if not PREF_PATH.exists():
        return PREF_DEFAULTS.copy()
    try:
        with open(PREF_PATH, "rb") as f:
            raw = f.read().strip()
        if not raw:
            return PREF_DEFAULTS.copy()
        decoded = base64.b64decode(raw)
        data = json.loads(decoded)
        # Ensure defaults
        for k, v in PREF_DEFAULTS.items():
            data.setdefault(k, v)
        # Migrate legacy single root into search_paths
        if data.get("project_root") and not data.get("search_paths"):
            data["search_paths"] = [data["project_root"]]
        return data
    except Exception:
        return PREF_DEFAULTS.copy()


pref_data = _load_prefs()


def _default_project_root() -> Path:
    if sys.platform.startswith("win"):
        return Path.home() / "AppData" / "Local" / "VR-Engine-Projects"
    return Path.home() / ".vr_engine_projects"


def _resolve_project_root(path_str: Optional[str]) -> Path:
    path = Path(path_str).expanduser() if path_str else _default_project_root()
    return path


_SEARCH_PATHS: list[Path] = [
    _resolve_project_root(p) for p in pref_data.get("search_paths", []) if p
]
if not _SEARCH_PATHS and pref_data.get("project_root"):
    _SEARCH_PATHS = [_resolve_project_root(pref_data.get("project_root"))]
if not _SEARCH_PATHS:
    _SEARCH_PATHS = [_default_project_root()]
for p in _SEARCH_PATHS:
    p.mkdir(exist_ok=True, parents=True)
pref_data["search_paths"] = [str(p) for p in _SEARCH_PATHS]
pref_data["project_root"] = (
    pref_data.get("project_root") or pref_data["search_paths"][0]
)
PROJECTS_DIR = _SEARCH_PATHS[0]


def _get_search_paths() -> list[Path]:
    return list(_SEARCH_PATHS)


def _set_search_paths(paths: list[Path]):
    global _SEARCH_PATHS, PROJECTS_DIR
    _SEARCH_PATHS = [p.resolve() for p in paths if p]
    if not _SEARCH_PATHS:
        _SEARCH_PATHS = [_default_project_root()]
    for p in _SEARCH_PATHS:
        p.mkdir(parents=True, exist_ok=True)
    PROJECTS_DIR = _SEARCH_PATHS[0]
    pref_data["search_paths"] = [str(p) for p in _SEARCH_PATHS]
    pref_data["project_root"] = pref_data["search_paths"][0]


XR_SAMPLES_DIR = (Path(__file__).parent.parent / "XrSamples").resolve()
TEMPLATES_DIR = (Path(__file__).parent / "src" / "templates").resolve()


def _add_recent_project(path: Path):
    p_str = str(path.resolve())
    lst = pref_data.get("recent_projects", []) or []
    if p_str in lst:
        lst.remove(p_str)
    lst.insert(0, p_str)
    pref_data["recent_projects"] = lst[:30]


def _remove_recent_project(path: Path):
    p_str = str(path.resolve())
    pref_data["recent_projects"] = [
        p for p in pref_data.get("recent_projects", []) if p != p_str
    ]


def _available_projects():
    projects = []
    for root in _get_search_paths():
        if not root.exists():
            continue
        for entry in root.iterdir():
            if entry.is_dir() and (entry / "Src").exists():
                projects.append(entry)
    return sorted(projects, key=lambda p: p.name.lower())


def _find_project_by_name(name: str) -> Optional[Path]:
    for root in _get_search_paths():
        candidate = root / name
        if candidate.exists() and candidate.is_dir():
            return candidate
    return None


def exit_handler():
    try:
        with open(PREF_PATH, "wb") as f:
            f.write(base64.b64encode(json.dumps(pref_data, indent=4).encode()))
    except Exception:
        pass


atexit.register(exit_handler)


class ProjectTile(QFrame):
    clicked = Signal(str)

    def __init__(self, path: Path):
        super().__init__()
        self.path = path
        self.setObjectName("ProjectTile")
        self.setProperty("tileType", "project")
        self.setProperty("selected", False)
        self.setCursor(Qt.PointingHandCursor)
        self.setMinimumSize(140, 140)
        self.setMaximumSize(200, 200)
        self._shadow = QGraphicsDropShadowEffect(self)
        self._shadow.setBlurRadius(18)
        self._shadow.setOffset(0, 6)
        self._shadow.setColor(QColor(0, 0, 0, 160))
        self.setGraphicsEffect(self._shadow)
        self._blurAnim: QPropertyAnimation | None = None

        layout = QVBoxLayout()
        layout.setContentsMargins(10, 10, 10, 10)
        layout.setSpacing(6)
        self.setLayout(layout)
        name = QLabel(path.name)
        name.setAlignment(Qt.AlignCenter)
        name.setWordWrap(True)
        name.setObjectName("TileTitle")
        layout.addStretch(1)
        layout.addWidget(name, 0, Qt.AlignCenter)
        layout.addStretch(1)

    def mousePressEvent(self, event):
        if event.button() == Qt.LeftButton:
            self.clicked.emit(str(self.path))
        super().mousePressEvent(event)

    def setSelected(self, selected: bool):
        self.setProperty("selected", selected)
        self._shadow.setColor(
            QColor(77, 130, 255, 220) if selected else QColor(0, 0, 0, 160)
        )
        self.animateBlur(34 if selected else 18)
        self.style().unpolish(self)
        self.style().polish(self)

    def enterEvent(self, event):
        if not self.property("selected"):
            self.animateBlur(26)
        super().enterEvent(event)

    def leaveEvent(self, event):
        if not self.property("selected"):
            self.animateBlur(18)
        super().leaveEvent(event)

    def animateBlur(self, target: int):
        if self._blurAnim and self._blurAnim.state() == QPropertyAnimation.Running:
            self._blurAnim.stop()
        self._blurAnim = QPropertyAnimation(self._shadow, b"blurRadius", self)
        self._blurAnim.setStartValue(self._shadow.blurRadius())
        self._blurAnim.setEndValue(target)
        self._blurAnim.setDuration(180)
        self._blurAnim.setEasingCurve(QEasingCurve.OutCubic)
        self._blurAnim.start()


class NewProjectTile(QFrame):
    requestNew = Signal()

    def __init__(self):
        super().__init__()
        self.setObjectName("NewProjectTile")
        self.setProperty("tileType", "new")
        self.setCursor(Qt.PointingHandCursor)
        self.setMinimumSize(140, 140)
        self.setMaximumSize(200, 200)
        self._shadow = QGraphicsDropShadowEffect(self)
        self._shadow.setBlurRadius(24)
        self._shadow.setOffset(0, 8)
        self._shadow.setColor(QColor(48, 81, 138, 180))
        self.setGraphicsEffect(self._shadow)
        self._blurAnim: QPropertyAnimation | None = None

        layout = QVBoxLayout()
        layout.setContentsMargins(10, 10, 10, 10)
        layout.setSpacing(6)
        self.setLayout(layout)
        plus = QLabel("+")
        plus.setAlignment(Qt.AlignCenter)
        plus.setObjectName("PlusIcon")
        txt = QLabel("New Project")
        txt.setAlignment(Qt.AlignCenter)
        txt.setObjectName("TileTitle")
        layout.addStretch(1)
        layout.addWidget(plus)
        layout.addWidget(txt)
        layout.addStretch(1)

    def mousePressEvent(self, event):
        if event.button() == Qt.LeftButton:
            self.requestNew.emit()
        super().mousePressEvent(event)

    def enterEvent(self, event):
        self.animateBlur(34)
        super().enterEvent(event)

    def leaveEvent(self, event):
        self.animateBlur(24)
        super().leaveEvent(event)

    def animateBlur(self, target: int):
        if self._blurAnim and self._blurAnim.state() == QPropertyAnimation.Running:
            self._blurAnim.stop()
        self._blurAnim = QPropertyAnimation(self._shadow, b"blurRadius", self)
        self._blurAnim.setStartValue(self._shadow.blurRadius())
        self._blurAnim.setEndValue(target)
        self._blurAnim.setDuration(200)
        self._blurAnim.setEasingCurve(QEasingCurve.OutCubic)
        self._blurAnim.start()


class ProjectManager(QWidget):
    projectSelected = Signal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("VR Engine - Project Manager")
        self._ensure_search_paths()
        w, h = pref_data.get("window_size", [1100, 720])
        try:
            self.resize(int(w), int(h))
        except Exception:
            self.resize(1100, 720)

        self._tiles: list[ProjectTile] = []
        self._selected: Path | None = None
        sel = pref_data.get("selected_project")
        if sel and Path(sel).exists():
            self._selected = Path(sel)

        self._buildUi()
        QTimer.singleShot(0, self.rebuildProjectsGrid)
        self._applyTheme()

    def _ensure_search_paths(self):
        """On first run ask where projects should live; supports multiple roots."""
        global PROJECTS_DIR, _SEARCH_PATHS
        confirmed = pref_data.get("project_root_confirmed")
        paths = [Path(p) for p in pref_data.get("search_paths", []) if p]
        if confirmed and paths:
            _SEARCH_PATHS = [p for p in paths if p.exists()] or paths
        else:
            default_root = _default_project_root()
            selected = QFileDialog.getExistingDirectory(
                self,
                "Select folder to store VR Engine projects",
                str(default_root),
            )
            chosen = Path(selected) if selected else default_root
            chosen.mkdir(parents=True, exist_ok=True)
            _SEARCH_PATHS = [chosen]
            pref_data["project_root_confirmed"] = True
        for p in _SEARCH_PATHS:
            p.mkdir(exist_ok=True, parents=True)
        pref_data["search_paths"] = [str(p.resolve()) for p in _SEARCH_PATHS]
        pref_data["project_root"] = pref_data["search_paths"][0]
        PROJECTS_DIR = _SEARCH_PATHS[0]

    def _open_locations_dialog(self):
        class LocationsDialog(QDialog):
            def __init__(self, paths: list[Path], parent=None):
                super().__init__(parent)
                self.setWindowTitle("Project Locations")
                self.resize(520, 360)
                layout = QVBoxLayout(self)
                self.list = QListWidget(self)
                for p in paths:
                    self.list.addItem(str(p))
                layout.addWidget(self.list)

                btnRow = QHBoxLayout()
                self.addBtn = QPushButton("Add")
                self.removeBtn = QPushButton("Remove")
                self.upBtn = QPushButton("Up")
                self.downBtn = QPushButton("Down")
                btnRow.addWidget(self.addBtn)
                btnRow.addWidget(self.removeBtn)
                btnRow.addWidget(self.upBtn)
                btnRow.addWidget(self.downBtn)
                layout.addLayout(btnRow)

                buttons = QDialogButtonBox(
                    QDialogButtonBox.Ok | QDialogButtonBox.Cancel
                )
                layout.addWidget(buttons)

                self.addBtn.clicked.connect(self._add_path)
                self.removeBtn.clicked.connect(self._remove_selected)
                self.upBtn.clicked.connect(self._move_up)
                self.downBtn.clicked.connect(self._move_down)
                buttons.accepted.connect(self.accept)
                buttons.rejected.connect(self.reject)

            def _add_path(self):
                chosen = QFileDialog.getExistingDirectory(
                    self, "Add project folder", str(_default_project_root())
                )
                if chosen:
                    p = Path(chosen)
                    if str(p) not in [
                        self.list.item(i).text() for i in range(self.list.count())
                    ]:
                        self.list.addItem(str(p))

            def _remove_selected(self):
                row = self.list.currentRow()
                if row >= 0:
                    self.list.takeItem(row)

            def _move_up(self):
                row = self.list.currentRow()
                if row > 0:
                    item = self.list.takeItem(row)
                    self.list.insertItem(row - 1, item)
                    self.list.setCurrentRow(row - 1)

            def _move_down(self):
                row = self.list.currentRow()
                if row < self.list.count() - 1:
                    item = self.list.takeItem(row)
                    self.list.insertItem(row + 1, item)
                    self.list.setCurrentRow(row + 1)

            def result_paths(self) -> list[Path]:
                return [
                    Path(self.list.item(i).text()) for i in range(self.list.count())
                ]

        dlg = LocationsDialog(_get_search_paths(), self)
        if dlg.exec() == QDialog.Accepted:
            new_paths = dlg.result_paths()
            if not new_paths:
                QMessageBox.warning(self, "Locations", "At least one path is required.")
                return
            _set_search_paths(new_paths)
            pref_data["project_root_confirmed"] = True
            self.rebuildProjectsGrid()

    # -------- UI construction --------
    def _buildUi(self):
        rootLayout = QVBoxLayout()
        rootLayout.setContentsMargins(0, 0, 0, 0)
        rootLayout.setSpacing(0)
        self.setLayout(rootLayout)

        header = QFrame()
        header.setObjectName("HeaderBar")
        hLayout = QHBoxLayout()
        hLayout.setContentsMargins(12, 8, 12, 8)
        hLayout.setSpacing(10)
        header.setLayout(hLayout)
        title = QLabel("VR Engine Project Manager")
        title.setObjectName("Header")
        hLayout.addWidget(title, stretch=1)
        self.locationsBtn = QPushButton("Locations")
        self.locationsBtn.clicked.connect(self._open_locations_dialog)
        self.refreshBtn = QPushButton("Refresh")
        self.refreshBtn.clicked.connect(self.rebuildProjectsGrid)
        self.newBtn = QPushButton("New Project")
        self.newBtn.clicked.connect(self.createNewProject)
        hLayout.addWidget(self.locationsBtn)
        hLayout.addWidget(self.refreshBtn)
        hLayout.addWidget(self.newBtn)
        rootLayout.addWidget(header)

        contentWrap = QHBoxLayout()
        contentWrap.setContentsMargins(12, 8, 12, 12)
        contentWrap.setSpacing(16)
        rootLayout.addLayout(contentWrap, stretch=1)

        self.leftSide = QVBoxLayout()
        self.leftSide.setSpacing(8)
        contentWrap.addLayout(self.leftSide, stretch=4)

        self.inlineNewBtn = QToolButton()
        self.inlineNewBtn.setText("+ New Project")
        self.inlineNewBtn.setObjectName("InlineNew")
        self.inlineNewBtn.clicked.connect(self.createNewProject)
        self.inlineNewBtn.hide()
        self.leftSide.addWidget(self.inlineNewBtn, 0, Qt.AlignLeft)

        self.scrollArea = QScrollArea()
        self.scrollArea.setWidgetResizable(True)
        self.scrollWidget = QWidget()
        self.gridLayout = QGridLayout()
        self.gridLayout.setContentsMargins(0, 0, 0, 0)
        self.gridLayout.setSpacing(12)
        self.scrollWidget.setLayout(self.gridLayout)
        self.scrollWidget.installEventFilter(self)
        self.scrollArea.setWidget(self.scrollWidget)
        self.leftSide.addWidget(self.scrollArea, stretch=1)

        self.rightFrame = QFrame()
        self.rightFrame.setObjectName("RightPanel")
        self.rightFrame.setMinimumWidth(260)
        self.rightLayout = QVBoxLayout()
        self.rightLayout.setContentsMargins(16, 16, 16, 16)
        self.rightLayout.setSpacing(12)
        self.rightFrame.setLayout(self.rightLayout)
        contentWrap.addWidget(self.rightFrame, stretch=0)

        self.panelTitle = QLabel("Project Options")
        self.panelTitle.setObjectName("PanelHeader")
        self.rightLayout.addWidget(self.panelTitle)
        self.selectedLabel = QLabel("None selected")
        self.selectedLabel.setObjectName("SelectedPath")
        self.rightLayout.addWidget(self.selectedLabel)

        self.openBtn = QPushButton("Open")
        self.modifyBtn = QPushButton("Modify Config")
        self.deleteBtn = QPushButton("Delete")
        self.revealBtn = QPushButton("Open Folder")
        for b in (self.openBtn, self.modifyBtn, self.deleteBtn, self.revealBtn):
            b.setEnabled(False)
            self.rightLayout.addWidget(b)
        self.openBtn.clicked.connect(self._open_selected)
        self.modifyBtn.clicked.connect(self._modify_selected)
        self.deleteBtn.clicked.connect(self._delete_selected)
        self.revealBtn.clicked.connect(self._reveal_selected)

        self.rightLayout.addStretch(1)
        self._panelOpacity = QGraphicsOpacityEffect(self.rightFrame)
        self._panelOpacity.setOpacity(0.0)
        self.rightFrame.setGraphicsEffect(self._panelOpacity)
        self.rightFrame.hide()
        self._panelAnim: QPropertyAnimation | None = None

    # -------- Project listing --------
    def rebuildProjectsGrid(self):
        while self.gridLayout.count():
            item = self.gridLayout.takeAt(0)
            w = item.widget()
            if w:
                w.deleteLater()
        self._tiles.clear()

        available = {p.name: p for p in _available_projects()}
        ordered: list[Path] = []
        for p_str in pref_data.get("recent_projects", []):
            p = Path(p_str)
            if p.name in available:
                ordered.append(available.pop(p.name))
        ordered.extend(sorted(available.values(), key=lambda p: p.name.lower()))

        show_inline_new = len(ordered) > 0
        self.inlineNewBtn.setVisible(show_inline_new)

        if not ordered:
            newTile = NewProjectTile()
            newTile.requestNew.connect(self.createNewProject)
            self.gridLayout.addWidget(newTile, 0, 0)
            self._tiles = []
            self.clear_selection()
            return

        available_width = self.scrollArea.viewport().width() or 1
        card_width = 200
        columns = max(1, available_width // (card_width + 24))
        row = col = 0
        for path in ordered:
            tile = ProjectTile(path)
            tile.clicked.connect(self.on_tile_selected)
            self.gridLayout.addWidget(tile, row, col)
            self._tiles.append(tile)
            col += 1
            if col >= columns:
                col = 0
                row += 1

        if self._selected:
            self._selected = self._selected if self._selected.exists() else None
        if self._selected:
            matched = False
            for t in self._tiles:
                sel = Path(str(t.path)) == self._selected
                t.setSelected(sel)
                matched = matched or sel
            if matched:
                self.selectedLabel.setText(self._selected.name)
                self._set_selection_state(True)
            else:
                self.clear_selection()
        else:
            self.clear_selection()

    def on_tile_selected(self, path: str):
        self._selected = Path(path)
        pref_data["selected_project"] = str(self._selected)
        for t in self._tiles:
            t.setSelected(str(t.path) == path)
        self._set_selection_state(True)
        self.selectedLabel.setText(self._selected.name)
        _add_recent_project(self._selected)

    def clear_selection(self):
        self._selected = None
        pref_data["selected_project"] = ""
        for t in self._tiles:
            t.setSelected(False)
        self._set_selection_state(False)
        self.selectedLabel.setText("None selected")

    def eventFilter(self, source, event):
        if source is self.scrollWidget and event.type() == QEvent.MouseButtonPress:
            if event.button() == Qt.LeftButton:
                w = self.scrollWidget.childAt(event.pos())
                tile = None
                while w:
                    if isinstance(w, ProjectTile):
                        tile = w
                        break
                    w = w.parent()
                if not tile:
                    self.clear_selection()
        return super().eventFilter(source, event)

    def resizeEvent(self, event):
        super().resizeEvent(event)
        pref_data["window_size"] = [self.width(), self.height()]
        QTimer.singleShot(0, self.rebuildProjectsGrid)

    # -------- Buttons --------
    def _set_selection_state(self, enabled: bool):
        if enabled:
            if not self.rightFrame.isVisible():
                self.rightFrame.show()
            self._animate_panel(1.0)
        else:
            self._animate_panel(0.0, hide=True)
        for b in (self.openBtn, self.modifyBtn, self.deleteBtn, self.revealBtn):
            b.setEnabled(enabled)

    def _animate_panel(self, target: float, hide: bool = False):
        if self._panelAnim and self._panelAnim.state() == QPropertyAnimation.Running:
            self._panelAnim.stop()
        self._panelAnim = QPropertyAnimation(self._panelOpacity, b"opacity", self)
        self._panelAnim.setStartValue(self._panelOpacity.opacity())
        self._panelAnim.setEndValue(target)
        self._panelAnim.setDuration(
            200 if target > self._panelOpacity.opacity() else 160
        )
        self._panelAnim.setEasingCurve(QEasingCurve.OutCubic)
        if hide:
            self._panelAnim.finished.connect(lambda: self.rightFrame.hide())
        else:
            self.rightFrame.show()
        self._panelAnim.start()

    # -------- Actions --------
    def _open_selected(self):
        if not self._selected:
            return
        self.loadProject(self._selected)

    def _modify_selected(self):
        if not self._selected:
            return
        self.editProjectConfig(self._selected)

    def _delete_selected(self):
        if not self._selected:
            return
        self.deleteProject(self._selected)

    def _reveal_selected(self):
        if not self._selected:
            return
        try:
            os.startfile(str(self._selected))
        except Exception as e:
            QMessageBox.warning(self, "Open Folder", str(e))

    # -------- Legacy actions wired to new UI --------
    def loadProject(self, project: Path | str):
        project_path = (
            project if isinstance(project, Path) else _find_project_by_name(project)
        )
        if project_path is None or not project_path.exists():
            QMessageBox.warning(self, "Open", f"Project not found: {project}")
            return
        self.projectViewer = ProjectViewer(project_path, self)
        self.projectViewer.show()
        _add_recent_project(project_path)
        pref_data["last_project"] = project_path.name
        self.projectSelected.emit(project_path.name)

    def deleteProject(self, project_path: Path):
        project_name = project_path.name
        confirm = QMessageBox.question(
            self,
            "Delete Project",
            f"Delete project '{project_name}'? This cannot be undone.",
            QMessageBox.Yes | QMessageBox.No,
        )
        if confirm != QMessageBox.Yes:
            return
        try:
            shutil.rmtree(project_path, ignore_errors=True)
            shutil.rmtree(XR_SAMPLES_DIR / project_name, ignore_errors=True)
            _remove_recent_project(project_path)
            if pref_data.get("last_project") == project_name:
                pref_data["last_project"] = ""
            self.clear_selection()
            self.rebuildProjectsGrid()
        except Exception as e:
            QMessageBox.critical(self, "Delete Project", f"Failed to delete: {e}")

    def editProjectConfig(self, project_path: Path):
        project_name = project_path.name

        class EditConfigDialog(QDialog):
            def __init__(self, project_name: str, parent=None):
                super().__init__(parent)
                self.setWindowTitle(f"Edit Project Config: {project_name}")
                self.setModal(True)
                self.projectNameInput = QLineEdit(self)
                self.projectNameInput.setText(project_name)

                warn_manifest_override = None
                if (
                    PROJECTS_DIR / project_name / "AndroidManifest_injector.xml"
                ).exists():
                    warn_manifest_override = QLabel(
                        "Warning: manifest already exists; replacing is destructive",
                        self,
                    )
                    warn_manifest_override.setStyleSheet("color: #ff4400;")
                    warn_manifest_override.setWordWrap(True)

                self.manifest_template_choice = QComboBox(self)
                self.manifest_template_choice.addItems(
                    [
                        "No change (keep existing)",
                        "Default (Basic VR features)",
                        "Full (All features)",
                    ]
                )

                buttons = QDialogButtonBox(
                    QDialogButtonBox.Ok | QDialogButtonBox.Cancel, self
                )
                buttons.accepted.connect(self.accept)
                buttons.rejected.connect(self.reject)

                form = QFormLayout()
                form.addRow("Project name:", self.projectNameInput)
                if warn_manifest_override:
                    form.addRow(warn_manifest_override)
                form.addRow("Android Manifest:", self.manifest_template_choice)
                form.addRow(buttons)
                self.setLayout(form)
                self.resize(420, 0)

        dlg = EditConfigDialog(project_name, self)
        dlg.exec()

        if dlg.result() == QDialog.Accepted:
            new_project_name = dlg.projectNameInput.text().strip()
            current_path = PROJECTS_DIR / project_name
            target_path = (
                PROJECTS_DIR / new_project_name if new_project_name else current_path
            )
            if new_project_name and new_project_name != project_name:
                if target_path.exists():
                    QMessageBox.warning(
                        self, "Rename", "A project with that name already exists."
                    )
                    return
                try:
                    current_path.rename(target_path)
                    shutil.rmtree(XR_SAMPLES_DIR / project_name, ignore_errors=True)
                    project_path = target_path
                    project_name = new_project_name
                    _remove_recent_project(current_path)
                    _add_recent_project(project_path)
                except Exception as e:
                    QMessageBox.critical(self, "Rename Failed", str(e))
                    return

            warn_manifest_override = None
            if dlg.manifest_template_choice.currentIndex() == 1:
                manifest_src = TEMPLATES_DIR / "AndroidManifest_default.xml"
            elif dlg.manifest_template_choice.currentIndex() == 2:
                manifest_src = TEMPLATES_DIR / "AndroidManifest_full.xml"

            if manifest_src:
                try:
                    shutil.copy2(
                        manifest_src, project_path / "AndroidManifest_injector.xml"
                    )
                except Exception as e:
                    QMessageBox.critical(
                        self, "Manifest", f"Failed to apply manifest: {e}"
                    )

            self._selected = project_path
            self.rebuildProjectsGrid()

    def createNewProject(self):
        class NewProjectDialog(QDialog):
            def __init__(self, existing: list[str], parent=None):
                super().__init__(parent)
                self.setWindowTitle("New Project")
                self.setModal(True)

                self.projectNameInput = QLineEdit(self)
                self.projectNameInput.setPlaceholderText("my_vr_project")

                self.templateSelectionChoice = QComboBox(self)
                self.templateSelectionChoice.addItems(
                    [
                        "Engine Override (basic)",
                        "Engine Injection (advanced)",
                    ]
                )
                self.manifestModeSelectionChoice = QComboBox(self)
                self.manifestModeSelectionChoice.addItems(
                    [
                        "Don't override (use engine defaults)",
                        "Default (Basic VR features)",
                        "Full (All features)",
                    ]
                )

                self.errorLabel = QLabel("", self)
                self.errorLabel.setStyleSheet("color: #c00;")
                self.errorLabel.setWordWrap(True)

                buttons = QDialogButtonBox(
                    QDialogButtonBox.Ok | QDialogButtonBox.Cancel, self
                )
                self.okButton = buttons.button(QDialogButtonBox.Ok)
                self.okButton.setText("Create")
                self.okButton.setEnabled(False)
                buttons.accepted.connect(self._tryAccept)
                buttons.rejected.connect(self.reject)

                form = QFormLayout(self)
                form.addRow("Project name:", self.projectNameInput)
                form.addRow("Template:", self.templateSelectionChoice)
                form.addRow("Android Manifest:", self.manifestModeSelectionChoice)
                form.addRow(self.errorLabel)
                form.addRow(buttons)
                self.setLayout(form)
                self.resize(420, 0)

                self.projectNameInput.textChanged.connect(
                    lambda: self._validate(existing)
                )
                self._validate(existing)
                self.projectNameInput.setFocus()

            def _validate(self, existing: list[str]):
                name = self.projectNameInput.text().strip()
                if not name:
                    self.errorLabel.setText("Enter a project name.")
                    self.okButton.setEnabled(False)
                    return
                if not re.fullmatch(r"[A-Za-z0-9_\-]+", name):
                    self.errorLabel.setText("Use only letters, numbers, _, -.")
                    self.okButton.setEnabled(False)
                    return
                if name in existing:
                    self.errorLabel.setText(f"'{name}' already exists.")
                    self.okButton.setEnabled(False)
                    return
                self.errorLabel.setText("")
                self.okButton.setEnabled(True)

            def _tryAccept(self):
                self.accept() if self.okButton.isEnabled() else None

        existing_names = [p.name for p in _available_projects()]
        dlg = NewProjectDialog(existing_names, self)
        if dlg.exec() == QMessageBox.Accepted:
            project_name = dlg.projectNameInput.text().strip()
            project_path = PROJECTS_DIR / project_name
            template_choice = dlg.templateSelectionChoice.currentText()
            manifest_idx = dlg.manifestModeSelectionChoice.currentIndex()

            src_override = TEMPLATES_DIR / "engine_override"
            src_injection = TEMPLATES_DIR / "engine_injection"
            manifest_src = None
            if manifest_idx == 1:
                manifest_src = TEMPLATES_DIR / "AndroidManifest_default.xml"
            elif manifest_idx == 2:
                manifest_src = TEMPLATES_DIR / "AndroidManifest_full.xml"

            try:
                if project_path.exists():
                    shutil.rmtree(project_path)
                if template_choice == "Engine Override (basic)":
                    shutil.copytree(src_override, project_path)
                else:
                    shutil.copytree(src_injection, project_path)
                if manifest_src:
                    shutil.copy2(
                        manifest_src, project_path / "AndroidManifest_injector.xml"
                    )
                _add_recent_project(project_path)
                pref_data["selected_project"] = str(project_path)
                self._selected = project_path
                self.rebuildProjectsGrid()
                self.projectViewer = ProjectViewer(project_path, self)
                self.projectViewer.show()
            except Exception as e:
                QMessageBox.critical(
                    self, "New Project", f"Failed to create project:\n{e}"
                )

    # -------- Theming --------
    def _applyTheme(self):
        ss = """
        QWidget { background-color: #141414; color: #e0e0e0; font-family: 'Segoe UI', Arial; }
        QLabel { background: transparent; }
        QLabel#Header { font-size: 16px; font-weight: 600; }
        QLabel#PanelHeader { font-size: 17px; font-weight: 600; background: #1f1f1f; border: 1px solid #2a2a2a; border-radius: 6px; padding: 6px 10px; }
        QLabel#SelectedPath { font-size: 13px; color: #9aa0aa; }
        QFrame#HeaderBar { background: #1c1c1c; border-bottom: 1px solid #2b2b2b; }
        QFrame#RightPanel { background: #1a1a1a; border: 1px solid #262626; border-radius: 10px; }
        QPushButton, QToolButton { background: #222; border: 1px solid #333; border-radius: 6px; padding: 6px 10px; }
        QPushButton:hover, QToolButton:hover { background: #2c2c2c; }
        QPushButton:pressed, QToolButton:pressed { background: #353535; }
        QScrollArea { border: none; }
        QFrame[ tileType="project" ] { background: #1c1c1c; border: 1px solid #262626; border-radius: 12px; }
        QFrame[ tileType="project" ]:hover { border-color: #3a3a3a; }
        QFrame[ tileType="project" ][ selected="true" ] { border: 2px solid #4d82ff; background: #20283a; }
        QFrame[ tileType="new" ] { background: #18243a; border: 2px dashed #30518a; border-radius: 12px; }
        QFrame[ tileType="new" ]:hover { background: #20314d; }
        QLabel#PlusIcon { font-size: 34px; color: #4d82ff; }
        QLabel#TileTitle { font-size: 14px; font-weight: 500; background: #1d1d1d; border: 1px solid #2a2a2a; border-radius: 6px; padding: 4px 8px; }
        QToolButton#InlineNew { background: #18243a; border: 1px solid #26426d; border-radius: 6px; padding: 6px 10px; }
        QToolButton#InlineNew:hover { background: #20314d; }
        """
        self.setStyleSheet(ss)


class ProjectViewer(QWidget):
    # Console redirection helpers
    class _StreamEmitter(QObject):
        text = Signal(str)

    class _EmittingStream(io.TextIOBase):
        def __init__(self, emitter: "ProjectViewer._StreamEmitter"):
            super().__init__()
            self._emitter = emitter

        def write(self, s):
            if not s:
                return 0
            try:
                self._emitter.text.emit(str(s))
            except Exception:
                pass
            return len(s)

        def flush(self):
            return

    def __init__(self, project_path: Path | str, Mgr: ProjectManager, parent=None):
        super().__init__(parent)

        class CppHighlighter(QSyntaxHighlighter):
            def __init__(self, parent):
                super().__init__(parent)
                self.rules = []

                def fmt(color, bold=False, italic=False):
                    f = QTextCharFormat()
                    f.setForeground(QColor(color))
                    if bold:
                        f.setFontWeight(QFont.Bold)
                    if italic:
                        f.setFontItalic(True)
                    return f

                kw = [
                    "alignas",
                    "alignof",
                    "and",
                    "and_eq",
                    "asm",
                    "atomic_cancel",
                    "atomic_commit",
                    "atomic_noexcept",
                    "auto",
                    "bitand",
                    "bitor",
                    "bool",
                    "break",
                    "case",
                    "catch",
                    "char",
                    "char8_t",
                    "char16_t",
                    "char32_t",
                    "class",
                    "compl",
                    "concept",
                    "const",
                    "consteval",
                    "constexpr",
                    "constinit",
                    "const_cast",
                    "continue",
                    "co_await",
                    "co_return",
                    "co_yield",
                    "decltype",
                    "default",
                    "delete",
                    "do",
                    "double",
                    "dynamic_cast",
                    "else",
                    "enum",
                    "explicit",
                    "export",
                    "extern",
                    "false",
                    "float",
                    "for",
                    "friend",
                    "goto",
                    "if",
                    "inline",
                    "int",
                    "long",
                    "mutable",
                    "namespace",
                    "new",
                    "noexcept",
                    "not",
                    "not_eq",
                    "nullptr",
                    "operator",
                    "or",
                    "or_eq",
                    "private",
                    "protected",
                    "public",
                    "register",
                    "reinterpret_cast",
                    "requires",
                    "return",
                    "short",
                    "signed",
                    "sizeof",
                    "static",
                    "static_assert",
                    "static_cast",
                    "struct",
                    "switch",
                    "template",
                    "this",
                    "thread_local",
                    "throw",
                    "true",
                    "try",
                    "typedef",
                    "typeid",
                    "typename",
                    "union",
                    "unsigned",
                    "using",
                    "virtual",
                    "void",
                    "volatile",
                    "wchar_t",
                    "while",
                    "xor",
                    "xor_eq",
                ]
                kw_pat = r"\b(" + "|".join(kw) + r")\b"
                self.rules.append(
                    (QRegularExpression(kw_pat), fmt("#2a5db0", bold=True))
                )

                # Types like std::string, uint32_t, etc.
                self.rules.append(
                    (
                        QRegularExpression(r"\b(std::\w+|[A-Za-z_]\w*_t)\b"),
                        fmt("#7c3aed", False),
                    )
                )
                # Numbers
                self.rules.append(
                    (
                        QRegularExpression(r"\b(0x[0-9A-Fa-f]+|\d+(\.\d+)?)\b"),
                        fmt("#b45309"),
                    )
                )
                # Strings and chars
                self.rules.append(
                    (QRegularExpression(r"\"([^\"\\]|\\.)*\""), fmt("#0b8f00"))
                )
                self.rules.append(
                    (QRegularExpression(r"'([^'\\]|\\.)+'"), fmt("#0b8f00"))
                )
                # Preprocessor
                self.rules.append(
                    (QRegularExpression(r"^\s*#\s*\w+.*"), fmt("#8b5cf6", bold=True))
                )
                # Single-line comments
                self.rules.append(
                    (QRegularExpression(r"//[^\n]*"), fmt("#6b7280", italic=True))
                )
                # We'll handle multiline in highlightBlock

                self.commentStart = QRegularExpression(r"/\*")
                self.commentEnd = QRegularExpression(r"\*/")
                self.commentFormat = fmt("#6b7280", italic=True)

            def highlightBlock(self, text):
                # Simple rules
                for pattern, format_ in self.rules:
                    it = pattern.globalMatch(text)
                    while it.hasNext():
                        m = it.next()
                        self.setFormat(m.capturedStart(), m.capturedLength(), format_)

                # Multiline comments
                self.setCurrentBlockState(0)
                start = 0
                if self.previousBlockState() != 1:
                    startMatch = self.commentStart.match(text)
                    start = startMatch.capturedStart()
                else:
                    start = 0

                while start >= 0:
                    endMatch = self.commentEnd.match(text, start)
                    end = endMatch.capturedStart()
                    if end == -1:
                        self.setCurrentBlockState(1)
                        length = len(text) - start
                        self.setFormat(start, length, self.commentFormat)
                        break
                    else:
                        length = end - start + endMatch.capturedLength()
                        self.setFormat(start, length, self.commentFormat)
                        startMatch = self.commentStart.match(
                            text, end + endMatch.capturedLength()
                        )
                        start = startMatch.capturedStart()

        resolved_path = Path(project_path).resolve()
        self.project_path = resolved_path
        self.project_name = resolved_path.name
        self.Mgr = Mgr
        self.Mgr.hide()
        self.setWindowTitle(f"Project: {self.project_name}")
        self.layout = QVBoxLayout(self)

        # Settings
        self.settings = QSettings("MaybeBroken", "vr-engine-compiler")

        # Toolbar and header
        self.toolbar = QToolBar("Main", self)
        self.buildAction = QAction("Build", self)
        self.saveAction = QAction("Save", self)
        self.refreshAction = QAction("Refresh", self)
        self.openWithCodeAction = QAction("Open with Code", self)
        self.themeAction = QAction("Toggle Theme", self)
        self.showConsoleAction = QAction("Show Console", self)
        self.showConsoleAction.setCheckable(True)
        self.showConsoleAction.setChecked(True)
        self.showSidebarAction = QAction("Show Sidebar", self)
        self.showSidebarAction.setCheckable(True)
        self.showSidebarAction.setChecked(True)
        self.openExplorerAction = QAction("Open in Explorer", self)
        self.buildAction.setShortcut(QKeySequence("Ctrl+B"))
        self.saveAction.setShortcut(QKeySequence.Save)
        self.toolbar.addAction(self.buildAction)
        self.toolbar.addAction(self.saveAction)
        self.toolbar.addAction(self.refreshAction)
        self.toolbar.addAction(self.openWithCodeAction)
        self.toolbar.addSeparator()
        self.toolbar.addAction(self.openExplorerAction)
        self.toolbar.addSeparator()
        self.toolbar.addAction(self.themeAction)
        self.toolbar.addSeparator()
        self.toolbar.addAction(self.showSidebarAction)
        self.toolbar.addAction(self.showConsoleAction)
        self.layout.addWidget(self.toolbar)

        # Keep root cached so file model does not walk the entire filesystem.
        self.project_root = str(self.project_path)

        # Header row: keep fixed height so it doesn't expand vertically
        headerWidget = QWidget(self)
        headerWidget.setObjectName("headerRow")
        headerLayout = QHBoxLayout(headerWidget)
        headerLayout.setContentsMargins(8, 4, 8, 4)
        headerLayout.setSpacing(8)

        self.currentFileLabel = QLabel("No file opened", headerWidget)
        self.currentFileLabel.setStyleSheet("color: #666;")
        # Single-line label; don't let it drive vertical growth
        self.currentFileLabel.setWordWrap(False)
        self.currentFileLabel.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)

        headerLayout.addStretch(1)
        headerLayout.addWidget(self.currentFileLabel)

        # Ensure the whole header row stays at its size hint vertically
        headerWidget.setSizePolicy(QSizePolicy.Preferred, QSizePolicy.Fixed)
        self.layout.addWidget(headerWidget)

        # Splitter: left tree / right viewer + console
        self.mainSplitter = QSplitter(self)
        self.layout.addWidget(self.mainSplitter)

        # Left pane: filter + file tree
        leftPane = QWidget(self)
        leftLayout = QVBoxLayout(leftPane)
        leftLayout.setContentsMargins(4, 4, 4, 4)
        filterRow = QHBoxLayout()
        self.searchBox = QLineEdit(self)
        self.searchBox.setPlaceholderText("Filter files…")
        self.kindFilter = QComboBox(self)
        self.kindFilter.addItems(["All", "Code", "Assets"])
        filterRow.addWidget(self.searchBox)
        filterRow.addWidget(self.kindFilter)
        leftLayout.addLayout(filterRow)

        self.fsModel = QFileSystemModel(self)
        self.fsModel.setFilter(QDir.AllEntries | QDir.NoDotAndDotDot)
        self.fsModel.setRootPath(self.project_root)

        class FileFilterProxy(QSortFilterProxyModel):
            def __init__(self, parent, root_getter):
                super().__init__(parent)
                self._text = ""
                self._kind = "All"
                self._root_getter = root_getter
                self.setRecursiveFilteringEnabled(True)

            def setText(self, t):
                self._text = (t or "").lower()
                self.invalidateFilter()

            def setKind(self, k):
                self._kind = k
                self.invalidateFilter()

            def filterAcceptsRow(self, source_row, source_parent):
                src = self.sourceModel()
                idx = src.index(source_row, 0, source_parent)
                if not idx.isValid():
                    return False
                path = src.filePath(idx)
                name = os.path.basename(path).lower()
                if self._text and self._text not in name:
                    # allow directories through if any child matches
                    if src.isDir(idx):
                        for r in range(src.rowCount(idx)):
                            if self.filterAcceptsRow(r, idx):
                                return True
                    return False
                if self._kind == "Code":
                    code_ext = {
                        ".c",
                        ".cc",
                        ".cpp",
                        ".cxx",
                        ".h",
                        ".hh",
                        ".hpp",
                        ".hxx",
                        ".ipp",
                        ".inl",
                        ".tpp",
                    }
                    return src.isDir(idx) or os.path.splitext(name)[1] in code_ext
                if self._kind == "Assets":
                    root = self._root_getter()
                    try:
                        rel = os.path.relpath(path, root)
                    except Exception:
                        rel = path
                    if rel.split(os.sep)[0] == "assets":
                        return True
                    asset_ext = {
                        ".png",
                        ".jpg",
                        ".jpeg",
                        ".gif",
                        ".bmp",
                        ".tga",
                        ".ktx",
                        ".ktx2",
                        ".mp3",
                        ".wav",
                        ".ogg",
                        ".flac",
                        ".mp4",
                        ".webm",
                        ".mkv",
                        ".zip",
                        ".tar",
                        ".gz",
                        ".ttf",
                        ".otf",
                        ".woff",
                        ".woff2",
                        ".obj",
                        ".stl",
                        ".fbx",
                        ".gltf",
                        ".glb",
                    }
                    return os.path.splitext(name)[1] in asset_ext or src.isDir(idx)
                return True

        self.proxyModel = FileFilterProxy(self, lambda: self.project_root)
        self.proxyModel.setSourceModel(self.fsModel)
        self.tree = QTreeView(self)
        self.tree.setModel(self.proxyModel)
        self.tree.setRootIsDecorated(True)
        self.tree.setSortingEnabled(True)
        self.tree.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        leftLayout.addWidget(self.tree, 1)

        self.mainSplitter.addWidget(leftPane)
        # Prefer right side to stretch
        self.mainSplitter.setStretchFactor(0, 0)

        # Right pane (stacked viewers: editor, image, archive, font, media, info)
        rightPane = QWidget(self)
        rightLayout = QVBoxLayout(rightPane)
        rightLayout.setContentsMargins(0, 0, 0, 0)
        rightPane.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)

        self.viewerStack = QStackedWidget(self)
        self.viewerStack.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        rightLayout.addWidget(self.viewerStack)

        # 0 - Text editor (for code/text files)
        self.editor = QPlainTextEdit(self)
        self.editor.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        self.editor.setTabStopDistance(
            4 * self.editor.fontMetrics().horizontalAdvance(" ")
        )
        self.editor.textChanged.connect(self._onTextChanged)
        self.viewerStack.addWidget(self.editor)

        # 1 - Image viewer
        self.imageScroll = QScrollArea(self)
        self.imageScroll.setWidgetResizable(True)
        self.imageLabel = QLabel()
        self.imageLabel.setAlignment(Qt.AlignCenter)
        self.imageLabel.setBackgroundRole(self.imageLabel.backgroundRole())
        self.imageScroll.setWidget(self.imageLabel)
        self.imageScroll.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        self.viewerStack.addWidget(self.imageScroll)

        # 2 - Archive browser (zip/tar)
        self.archivePane = QWidget(self)

        _al = QVBoxLayout(self.archivePane)
        _al.setContentsMargins(0, 0, 0, 0)
        self.archiveToolbar = QToolBar(self.archivePane)
        self.actionExtract = QAction("Extract…", self.archiveToolbar)
        self.archiveToolbar.addAction(self.actionExtract)
        _al.addWidget(self.archiveToolbar)
        self.archiveTree = QTreeWidget(self.archivePane)
        self.archiveTree.setHeaderLabels(["Name", "Size", "Type"])
        _al.addWidget(self.archiveTree, 1)
        self.viewerStack.addWidget(self.archivePane)

        # 3 - Font preview
        self.fontPane = QWidget(self)
        _fl = QVBoxLayout(self.fontPane)
        _fl.setContentsMargins(8, 8, 8, 8)
        self.fontSample = QLabel(
            "The quick brown fox jumps over the lazy dog 0123456789", self.fontPane
        )
        self.fontSample.setWordWrap(True)
        self.fontSizeSlider = QSlider(Qt.Horizontal, self.fontPane)
        self.fontSizeSlider.setMinimum(6)
        self.fontSizeSlider.setMaximum(96)
        self.fontSizeSlider.setValue(20)
        _fl.addWidget(self.fontSample)
        _fl.addWidget(QLabel("Size:"))
        _fl.addWidget(self.fontSizeSlider)
        self.viewerStack.addWidget(self.fontPane)

        # 4 - Media player (audio/video)
        self.mediaPane = QWidget(self)
        _ml = QVBoxLayout(self.mediaPane)
        _ml.setContentsMargins(0, 0, 0, 0)
        self.mediaToolbar = QToolBar(self.mediaPane)
        self.actionPlayPause = QAction("Play", self.mediaToolbar)
        self.mediaToolbar.addAction(self.actionPlayPause)
        _ml.addWidget(self.mediaToolbar)
        # Try to import multimedia; if unavailable, we'll fallback to info view
        self._multimedia_ok = True
        try:
            from PySide6.QtMultimediaWidgets import QVideoWidget
            from PySide6.QtMultimedia import QMediaPlayer, QAudioOutput

            self._QVideoWidget = QVideoWidget
            self._QMediaPlayer = QMediaPlayer
            self._QAudioOutput = QAudioOutput
        except Exception:
            self._multimedia_ok = False
            self._QVideoWidget = None
            self._QMediaPlayer = None
            self._QAudioOutput = None
        if self._multimedia_ok:
            self.videoWidget = self._QVideoWidget(self.mediaPane)
            _ml.addWidget(self.videoWidget, 1)
        else:
            self.videoWidget = QLabel("QtMultimedia not available", self.mediaPane)
            self.videoWidget.setAlignment(Qt.AlignCenter)
            _ml.addWidget(self.videoWidget, 1)
        self.viewerStack.addWidget(self.mediaPane)

        # 5 - Generic info view
        self.infoView = QPlainTextEdit(self)
        self.infoView.setReadOnly(True)
        self.viewerStack.addWidget(self.infoView)

        # Right: vertical splitter for viewer and console
        self.rightSplitter = QSplitter(Qt.Vertical, self)
        self.rightSplitter.setChildrenCollapsible(False)
        self.rightSplitter.addWidget(rightPane)

        self.consolePane = QWidget(self)
        _cl = QVBoxLayout(self.consolePane)
        _cl.setContentsMargins(0, 0, 0, 0)
        self.consoleToolbar = QToolBar(self.consolePane)
        self.actionClearConsole = QAction("Clear", self.consoleToolbar)
        self.consoleToolbar.addAction(self.actionClearConsole)
        _cl.addWidget(self.consoleToolbar)
        self.console = QPlainTextEdit(self.consolePane)
        self.console.setReadOnly(True)
        self.console.setMaximumBlockCount(5000)
        self.console.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        _cl.addWidget(self.console, 1)
        self.rightSplitter.addWidget(self.consolePane)

        self.mainSplitter.addWidget(self.rightSplitter)
        self.mainSplitter.setStretchFactor(1, 1)
        self.rightSplitter.setStretchFactor(0, 3)
        self.rightSplitter.setStretchFactor(1, 1)

        # Syntax highlighter for C++
        self.highlighter = CppHighlighter(self.editor.document())

        # State
        self.currentFilePath = None
        self._dirty = False

        # Signals
        self.buildAction.triggered.connect(self.buildProject)
        self.saveAction.triggered.connect(self.saveCurrentFile)
        self.refreshAction.triggered.connect(self._refreshTree)
        self.openWithCodeAction.triggered.connect(self._openWithCode)
        self.actionClearConsole.triggered.connect(self.console.clear)
        self.themeAction.triggered.connect(self._toggleTheme)
        self.openExplorerAction.triggered.connect(self._openInExplorer)
        self.tree.doubleClicked.connect(self._onTreeDoubleClicked)
        self.searchBox.textChanged.connect(self.proxyModel.setText)
        self.kindFilter.currentTextChanged.connect(self.proxyModel.setKind)
        self.actionExtract.triggered.connect(self._extractArchive)
        self.fontSizeSlider.valueChanged.connect(self._updateFontSampleSize)
        self.actionPlayPause.triggered.connect(self._togglePlayPause)
        self.showConsoleAction.toggled.connect(self._setConsoleVisible)
        self.showSidebarAction.toggled.connect(self._setSidebarVisible)

        # Populate lists
        self.populateLists()

        # Media state
        self._player = None
        self._audioOut = None

        # Drag & drop
        self.setAcceptDrops(True)

        # Restore UI state and theme
        try:
            s = self.settings.value("splitterMain")
            if s:
                self.mainSplitter.restoreState(s)
            s2 = self.settings.value("splitterRight")
            if s2:
                self.rightSplitter.restoreState(s2)
            vConsole = self.settings.value("showConsole")
            if vConsole is not None:
                self.showConsoleAction.setChecked(bool(int(vConsole)))
            vSidebar = self.settings.value("showSidebar")
            if vSidebar is not None:
                self.showSidebarAction.setChecked(bool(int(vSidebar)))
        except Exception:
            pass
        self._applySavedTheme()

        # Redirect stdout/stderr to console
        self._consoleEmitter = ProjectViewer._StreamEmitter()
        self._consoleEmitter.text.connect(self._appendConsole)
        self._prevStdout = sys.stdout
        self._prevStderr = sys.stderr
        try:
            sys.stdout = ProjectViewer._EmittingStream(self._consoleEmitter)
            sys.stderr = ProjectViewer._EmittingStream(self._consoleEmitter)
        except Exception:
            pass

    def closeEvent(self, e):
        # Ask to save if dirty
        if self._dirty and self.currentFilePath:
            res = QMessageBox.question(
                self,
                "Unsaved changes",
                f"Save changes to {os.path.relpath(self.currentFilePath, self.project_root)}?",
                QMessageBox.Yes | QMessageBox.No | QMessageBox.Cancel,
            )
            if res == QMessageBox.Cancel:
                e.ignore()
                return
            if res == QMessageBox.Yes:
                self.saveCurrentFile()
        self.Mgr.show()
        try:
            self.settings.setValue("splitterMain", self.mainSplitter.saveState())
            self.settings.setValue("splitterRight", self.rightSplitter.saveState())
            self.settings.setValue(
                "showConsole", int(self.showConsoleAction.isChecked())
            )
            self.settings.setValue(
                "showSidebar", int(self.showSidebarAction.isChecked())
            )
        except Exception:
            pass
        # Restore stdout/stderr
        try:
            if hasattr(self, "_prevStdout") and self._prevStdout:
                sys.stdout = self._prevStdout
            if hasattr(self, "_prevStderr") and self._prevStderr:
                sys.stderr = self._prevStderr
        except Exception:
            pass
        super().closeEvent(e)

    def populateLists(self):
        try:
            self.fsModel.setRootPath(self.project_root)
        except Exception:
            pass
        root_idx = self.fsModel.index(self.project_root)
        proxy_root = self.proxyModel.mapFromSource(root_idx)
        self.tree.setRootIndex(proxy_root)
        # expand common dirs
        for name in ("Src", "assets"):
            p = os.path.join(self.project_root, name)
            if os.path.isdir(p):
                idx = self.fsModel.index(p)
                pidx = self.proxyModel.mapFromSource(idx)
                if pidx.isValid():
                    self.tree.setExpanded(pidx, True)

    def _refreshTree(self):
        self.fsModel.setRootPath(self.project_root)
        self.populateLists()

    def _openWithCode(self):
        def _th():
            os.system(f'code "{self.project_root}/Src/main.cpp" "{self.project_root}"')

        threading.Thread(target=_th, daemon=True).start()

    def _onTreeDoubleClicked(self, index):
        if not index.isValid():
            return
        sidx = self.proxyModel.mapToSource(index)
        path = self.fsModel.filePath(sidx)
        if os.path.isdir(path):
            self.tree.setExpanded(index, not self.tree.isExpanded(index))
            return
        self.openFile(path)

    def _is_text(self, path):
        # Heuristic: try utf-8 decode; if fail, but content mostly printable and has newlines, accept as text
        try:
            with open(path, "rb") as fh:
                chunk = fh.read(65536)
            try:
                chunk.decode("utf-8")
                return True
            except UnicodeDecodeError:
                txt = chunk.decode("latin-1", errors="ignore")
                printable = sum(1 for c in txt if c.isprintable() or c in "\r\n\t")
                return printable / max(1, len(txt)) > 0.9 and (
                    "\n" in txt or "\r" in txt
                )
        except Exception:
            return False

    # -------- Detection and routing --------
    def detect_file_type(self, path):

        result = {"type": "unknown", "subtype": None}
        try:
            with open(path, "rb") as f:
                head = f.read(4096)
        except Exception:
            return result

        # Images via QImageReader (sniffs header)
        try:
            if QImageReader.imageFormat(path):
                return {"type": "image"}
        except Exception:
            pass

        # Fonts
        if head.startswith(b"\x00\x01\x00\x00") or head.startswith(b"OTTO"):
            return {"type": "font", "subtype": "otf/ttf"}
        if head.startswith(b"wOFF"):
            return {"type": "font", "subtype": "woff"}
        if head.startswith(b"wOF2"):
            return {"type": "font", "subtype": "woff2"}

        # Archives
        if head.startswith(b"PK\x03\x04"):
            return {"type": "archive", "subtype": "zip"}
        if head.startswith(b"\x1f\x8b\x08"):
            return {"type": "archive", "subtype": "gzip"}
        if head.startswith(b"ustar") or b"ustar\x00" in head[:512]:
            return {"type": "archive", "subtype": "tar"}
        if head.startswith(b"7z\xbc\xaf'\x1c"):
            return {"type": "archive", "subtype": "7z"}
        if head.startswith(b"Rar!\x1a\x07\x00") or head.startswith(
            b"Rar!\x1a\x07\x01\x00"
        ):
            return {"type": "archive", "subtype": "rar"}

        # 3D models
        if head[:4] == b"glTF":
            return {"type": "3d", "subtype": "glb"}
        # OBJ or ASCII STL: try textual hints
        try:
            txt = head.decode("utf-8", errors="ignore")
            if txt.lstrip().startswith("{") and '"asset"' in txt and '"scenes"' in txt:
                return {"type": "3d", "subtype": "gltf"}
            if txt.lstrip().startswith("solid "):
                return {"type": "3d", "subtype": "stl-ascii"}
            if any(
                line.startswith(("v ", "vn ", "vt ", "f ", "mtllib ", "o "))
                for line in txt.splitlines()[:50]
            ):
                return {"type": "3d", "subtype": "obj"}
        except Exception:
            pass
        # Binary STL: 80-byte header then uint32 tri count; ambiguous but common
        if len(head) >= 84:
            tri = struct.unpack("<I", head[80:84])[0]
            if tri > 0 and tri < 10_000_000:
                return {"type": "3d", "subtype": "stl-bin"}
        # FBX
        if head.startswith(b"Kaydara FBX Binary  \x00\x1a\x00"):
            return {"type": "3d", "subtype": "fbx"}

        # Media (audio/video)
        if head.startswith(b"ID3") or head[:2] == b"\xff\xfb":
            return {"type": "media", "subtype": "mp3"}
        if head.startswith(b"RIFF") and b"WAVE" in head[8:16]:
            return {"type": "media", "subtype": "wav"}
        if head.startswith(b"OggS"):
            return {"type": "media", "subtype": "ogg"}
        if head[4:8] == b"ftyp":
            return {"type": "media", "subtype": "mp4/mov"}
        if head.startswith(b"fLaC"):
            return {"type": "media", "subtype": "flac"}
        if head.startswith(b"\x1a\x45\xdf\xa3"):
            return {"type": "media", "subtype": "mkv/webm"}

        # PDF / others
        if head.startswith(b"%PDF-"):
            return {"type": "doc", "subtype": "pdf"}

        # Text fallback
        if self._is_text(path):
            return {"type": "text"}

        return result

    def openFile(self, path):
        if self._dirty and self.currentFilePath:
            res = QMessageBox.question(
                self,
                "Unsaved changes",
                f"Save changes to {os.path.relpath(self.currentFilePath, self.project_root)}?",
                QMessageBox.Yes | QMessageBox.No | QMessageBox.Cancel,
            )
            if res == QMessageBox.Cancel:
                return
            if res == QMessageBox.Yes:
                self.saveCurrentFile()

        if not os.path.isfile(path):
            QMessageBox.warning(self, "Open File", "File not found.")
            return

        # Detect and route
        ftype = self.detect_file_type(path)
        self.currentFilePath = path
        self._dirty = False
        self._updateTitle()

        try:
            if ftype["type"] == "text":
                self._showTextFile(path)
            elif ftype["type"] == "image":
                self._showImage(path)
            elif ftype["type"] == "archive":
                self._showArchive(path, ftype.get("subtype"))
            elif ftype["type"] == "font":
                self._showFont(path)
            elif ftype["type"] in ("media",):
                self._showMedia(path)
            elif ftype["type"] in ("3d", "doc", "unknown"):
                # Provide generic info panel with some parsed metadata
                self._showInfo(path, ftype)
            else:
                self._showInfo(path, ftype)
        except Exception as e:
            QMessageBox.critical(self, "Open File", f"Failed to open file:\n{e}")

    def _updateTitle(self):
        rel = (
            os.path.relpath(self.currentFilePath, self.project_root)
            if self.currentFilePath
            else "No file opened"
        )
        mark = "*" if self._dirty else ""
        self.currentFileLabel.setText(f"{rel}{mark}")

    # Console helpers and panel visibility
    def _appendConsole(self, text: str):
        try:
            # Avoid excessive newlines formatting; insert as-is
            self.console.moveCursor(QTextCursor.End)
            self.console.insertPlainText(text)
            self.console.ensureCursorVisible()
        except Exception:
            pass

    def _setConsoleVisible(self, visible: bool):
        if not hasattr(self, "rightSplitter"):
            return
        if visible:
            # Restore some reasonable sizes if console was hidden
            sizes = self.rightSplitter.sizes()
            if sizes[1] == 0:
                self.rightSplitter.setSizes([max(1, sizes[0]), max(1, sizes[0] // 3)])
        else:
            self.rightSplitter.setSizes([1, 0])

    def _setSidebarVisible(self, visible: bool):
        if not hasattr(self, "mainSplitter"):
            return
        if visible:
            sizes = self.mainSplitter.sizes()
            if sizes[0] == 0:
                self.mainSplitter.setSizes([300, max(600, sizes[1])])
        else:
            self.mainSplitter.setSizes([0, max(1, sum(self.mainSplitter.sizes()))])

    # Theming helpers
    def _applySavedTheme(self):
        theme = self.settings.value("theme", "dark")
        self._applyTheme(theme)

    def _toggleTheme(self):
        cur = self.settings.value("theme", "dark")
        new = "light" if cur == "dark" else "dark"
        self.settings.setValue("theme", new)
        self._applyTheme(new)

    def _applyTheme(self, theme):
        if theme == "dark":
            self._setDarkStyle()
        else:
            self._setLightStyle()

    def _setDarkStyle(self):
        ss = """
            QWidget { background: #1e1e1e; color: #ddd; }
            QToolBar { background: #252526; border: 0; }
            QPlainTextEdit { background: #1b1b1b; border: 1px solid #333; }
            QTreeView { background: #1b1b1b; border: 1px solid #333; }
            QLineEdit { background: #2a2a2a; border: 1px solid #444; padding: 4px; }
        """
        self.setStyleSheet(ss)

    def _setLightStyle(self):
        ss = """
            QWidget { background: #fafafa; color: #222; }
            QToolBar { background: #f0f0f0; border: 0; }
            QPlainTextEdit { background: #ffffff; border: 1px solid #ccc; }
            QTreeView { background: #ffffff; border: 1px solid #ccc; }
            QLineEdit { background: #ffffff; border: 1px solid #bbb; padding: 4px; }
        """
        self.setStyleSheet(ss)

    def _openInExplorer(self):
        try:
            os.startfile(self.project_root)
        except Exception as e:
            QMessageBox.warning(self, "Explorer", str(e))

    # Drag & drop
    def dragEnterEvent(self, event):
        if event.mimeData().hasUrls():
            event.acceptProposedAction()
        else:
            super().dragEnterEvent(event)

    def dropEvent(self, event):
        urls = event.mimeData().urls()
        if urls:
            path = urls[0].toLocalFile()
            if path:
                self.openFile(path)
        super().dropEvent(event)

    def _onTextChanged(self):
        if self.currentFilePath is None:
            return
        if not self._dirty:
            self._dirty = True
            self._updateTitle()

    def saveCurrentFile(self):
        if not self.currentFilePath:
            return
        try:
            with open(self.currentFilePath, "w", encoding="utf-8") as f:
                f.write(self.editor.toPlainText())
            self._dirty = False
            self._updateTitle()
        except Exception as e:
            QMessageBox.critical(self, "Save File", f"Failed to save file:\n{e}")

    def buildProject(self):
        # Log to console
        try:
            if hasattr(self, "console"):
                self.console.appendPlainText("== Build started ==")
        except Exception:
            pass
        try:
            build_project(self.project_name, open_in_android_studio=True)
            if hasattr(self, "console"):
                self.console.appendPlainText("== Build finished ==")
        except Exception as e:
            if hasattr(self, "console"):
                self.console.appendPlainText(f"Build failed: {e}")
            raise

    # -------- View helpers --------
    def _setView(self, idx):
        self.viewerStack.setCurrentIndex(idx)

    def _showTextFile(self, path):
        try:
            text = None
            try:
                with open(path, "r", encoding="utf-8") as f:
                    text = f.read()
            except UnicodeDecodeError:
                with open(path, "r", encoding="latin-1", errors="replace") as f:
                    text = f.read()
            self.editor.blockSignals(True)
            self.editor.setPlainText(text or "")
            self.editor.blockSignals(False)
            self._dirty = False
            self._setView(0)
        except Exception as e:
            raise

    def _showImage(self, path):

        reader = QImageReader(path)
        reader.setAutoTransform(True)
        img = reader.read()
        if img.isNull():
            self._showInfo(path, {"type": "image", "error": reader.errorString()})
            return
        max_dim = 4096
        if img.width() > max_dim or img.height() > max_dim:
            img = img.scaled(
                max_dim, max_dim, Qt.KeepAspectRatio, Qt.SmoothTransformation
            )
        pix = QPixmap.fromImage(img)
        self.imageLabel.setPixmap(pix)
        self.imageLabel.adjustSize()
        self._setView(1)

    def _showArchive(self, path, subtype):

        self.archiveTree.clear()
        items = []
        handled = False
        if subtype == "zip" or (subtype is None and zipfile.is_zipfile(path)):
            handled = True
            with zipfile.ZipFile(path) as zf:
                for zi in zf.infolist():
                    size = zi.file_size
                    name = zi.filename
                    typ = "dir" if name.endswith("/") else "file"
                    it = QTreeWidgetItem([name, str(size), typ])
                    self.archiveTree.addTopLevelItem(it)
        elif subtype in ("tar", "gzip") or tarfile.is_tarfile(path):
            handled = True
            with tarfile.open(path, "r:*") as tf:
                for ti in tf.getmembers():
                    size = ti.size
                    name = ti.name
                    typ = "dir" if ti.isdir() else "file"
                    it = QTreeWidgetItem([name, str(size), typ])
                    self.archiveTree.addTopLevelItem(it)
        if not handled:
            # Unsupported archives (7z/rar)
            self.archiveTree.addTopLevelItem(
                QTreeWidgetItem(["Unsupported archive format", "", subtype or "?"])
            )
        self.archiveTree.resizeColumnToContents(0)
        self._setView(2)

    def _extractArchive(self):
        if self.viewerStack.currentIndex() != 2 or not self.currentFilePath:
            return

        dest = QFileDialog.getExistingDirectory(self, "Extract to…", self.project_root)
        if not dest:
            return

        try:
            if zipfile.is_zipfile(self.currentFilePath):
                with zipfile.ZipFile(self.currentFilePath) as zf:
                    zf.extractall(dest)
            elif tarfile.is_tarfile(self.currentFilePath):
                with tarfile.open(self.currentFilePath, "r:*") as tf:
                    tf.extractall(dest)
            else:
                QMessageBox.information(self, "Extract", "Unsupported archive type.")
                return
            QMessageBox.information(self, "Extract", f"Extracted to: {dest}")
        except Exception as e:
            QMessageBox.critical(self, "Extract", f"Extraction failed:\n{e}")

    def _showFont(self, path):

        db = QFontDatabase()
        fid = QFontDatabase.addApplicationFont(path)
        if fid < 0:
            self.fontSample.setText("Failed to load font.")
            self._setView(3)
            return
        fams = QFontDatabase.applicationFontFamilies(fid)
        fam = fams[0] if fams else None
        if fam:
            f = QFont(fam, self.fontSizeSlider.value())
            self.fontSample.setFont(f)
            self.fontSample.setText(self.fontSample.text())
        else:
            self.fontSample.setText("Loaded font, but no family reported.")
        self._setView(3)

    def _updateFontSampleSize(self, val):
        f = self.fontSample.font()
        f.setPointSize(val)
        self.fontSample.setFont(f)

    def _showMedia(self, path):
        if not self._multimedia_ok:
            self._showInfo(
                path, {"type": "media", "note": "QtMultimedia not available"}
            )
            return
        # Teardown old player if any
        try:
            if self._player is not None:
                self._player.stop()
        except Exception:
            pass

        self._player = self._QMediaPlayer(self)
        self._audioOut = self._QAudioOutput(self)
        self._player.setAudioOutput(self._audioOut)
        self._player.setVideoOutput(self.videoWidget)
        self._player.setSource(QUrl.fromLocalFile(os.path.abspath(path)))
        self.actionPlayPause.setText("Play")
        self._setView(4)

    def _togglePlayPause(self):
        if self.viewerStack.currentIndex() != 4 or not self._player:
            return
        st = self._player.playbackState()

        if st == QMediaPlayer.PlaybackState.PlayingState:
            self._player.pause()
            self.actionPlayPause.setText("Play")
        else:
            self._player.play()
            self.actionPlayPause.setText("Pause")

    def _showInfo(self, path, ftype):
        # Build a simple info dump with metadata and hex preview
        try:
            size = os.path.getsize(path)
        except Exception:
            size = None
        info = []
        info.append(f"Path: {path}")
        if size is not None:
            info.append(f"Size: {size} bytes")
        if ftype:
            info.append(f"Detected: {ftype}")
        try:
            with open(path, "rb") as f:
                head = f.read(512)
            hexstr = " ".join(f"{b:02X}" for b in head)
            info.append("\nHeader (first 512 bytes):")
            info.append(hexstr)
        except Exception:
            pass
        # Special cases: glb header
        try:
            with open(path, "rb") as f:
                head = f.read(12)
            if head[:4] == b"glTF":
                import struct

                version, length = struct.unpack("<II", head[4:12])
                info.append(f"\nGLB: version={version}, totalLength={length}")
        except Exception:
            pass
        self.infoView.setPlainText("\n".join(info))
        self._setView(5)


if __name__ == "__main__":

    app = QApplication(sys.argv)
    window = ProjectManager()
    window.show()
    sys.exit(app.exec())
