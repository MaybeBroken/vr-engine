import shutil
from PySide6.QtWidgets import (
    QWidget,
    QVBoxLayout,
    QPushButton,
    QListWidget,
    QMessageBox,
    QLineEdit,
    QComboBox as QSelectBox,
)
from PySide6.QtCore import Signal
from compile import build_project
import os
from PySide6.QtWidgets import QApplication
import sys
from PySide6.QtWidgets import QDialog, QDialogButtonBox, QLabel, QFormLayout
import re
from PySide6.QtCore import Qt, QRegularExpression


class ProjectManager(QWidget):
    projectSelected = Signal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Project Manager")
        self.layout = QVBoxLayout(self)

        self.projectList = QListWidget(self)
        self.layout.addWidget(self.projectList)

        self.loadProjectButton = QPushButton("Load Project", self)
        self.loadProjectButton.setDisabled(True)
        self.layout.addWidget(self.loadProjectButton)

        self.deleteProjectButton = QPushButton("Delete Project", self)
        self.deleteProjectButton.setDisabled(True)
        self.layout.addWidget(self.deleteProjectButton)

        self.newProjectButton = QPushButton("New Project +", self)
        self.layout.addWidget(self.newProjectButton)
        self.loadProjectButton.clicked.connect(
            lambda: self.loadProject(self.projectList.currentItem().text())
        )
        self.deleteProjectButton.clicked.connect(self.deleteProject)
        self.newProjectButton.clicked.connect(self.createNewProject)
        self.projectList.itemSelectionChanged.connect(
            lambda: [
                self.loadProjectButton.setDisabled(
                    self.projectList.currentItem() is None
                ),
                self.deleteProjectButton.setDisabled(
                    self.projectList.currentItem() is None
                ),
                self.loadProjectButton.setText(
                    f"Load Project '{self.projectList.currentItem().text()}'"
                ),
            ][-1]
        )

        self.populateProjectList()

    def getAvailableProjects(self):
        path = "./projects/"
        return [
            f.name
            for f in os.scandir(path)
            if f.is_dir() and os.path.exists(f.path + "/Src/")
        ]

    def loadProject(self, project_name: str):
        self.projectViewer = ProjectViewer(project_name, self)
        self.projectViewer.show()

    def deleteProject(self):
        project_name = self.projectList.currentItem().text()
        confirm = QMessageBox.question(
            self,
            "Delete Project",
            f"Are you sure you want to delete the project '{project_name}'? This action cannot be undone.",
            QMessageBox.Yes | QMessageBox.No,
        )
        if confirm == QMessageBox.Yes:
            shutil.rmtree(f"./projects/{project_name}/")
            self.populateProjectList()

    def populateProjectList(self):
        self.projectList.clear()
        for project_name in self.getAvailableProjects():
            self.projectList.addItem(project_name)
        self.loadProjectButton.setDisabled(True)
        self.loadProjectButton.setText("Load Project")
        self.deleteProjectButton.setDisabled(True)

    def createNewProject(self):
        class NewProjectDialog(QDialog):
            def __init__(self, parent=None):
                super().__init__(parent)
                self.setWindowTitle("New Project")
                self.setModal(True)

                self.projectNameInput = QLineEdit(self)
                self.projectNameInput.setPlaceholderText("my_vr_project")

                self.templateSelectionChoice = QSelectBox(self)
                self.templateSelectionChoice.addItems(
                    [
                        "Empty Project",
                        "Engine Override (basic)",
                        "Engine Injection (advanced)",
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
                form.addRow(self.errorLabel)
                form.addRow(buttons)
                self.setLayout(form)
                self.resize(380, 0)

                self.projectNameInput.textChanged.connect(self._validate)
                self._validate()
                self.projectNameInput.setFocus()

            def _validate(self):

                name = self.projectNameInput.text().strip()
                if not name:
                    self.errorLabel.setText("Enter a project name.")
                    self.okButton.setEnabled(False)
                    return

                if not re.fullmatch(r"[A-Za-z0-9_\-]+", name):
                    self.errorLabel.setText("Use only letters, numbers, _, -.")
                    self.okButton.setEnabled(False)
                    return

                existing = []
                parent = self.parent()
                try:
                    if parent and hasattr(parent, "getAvailableProjects"):
                        existing = parent.getAvailableProjects()
                except Exception:
                    pass

                if name in existing:
                    self.errorLabel.setText(f"'{name}' already exists.")
                    self.okButton.setEnabled(False)
                    return

                self.errorLabel.setText("")
                self.okButton.setEnabled(True)

            def _tryAccept(self):
                self._validate()
                if self.okButton.isEnabled():
                    self.accept()

        dlg = NewProjectDialog(self)
        if dlg.exec() == QMessageBox.Accepted:
            project_name = dlg.projectNameInput.text()
            template = dlg.templateSelectionChoice.currentText()
            if template == "Empty Project":
                shutil.copytree(
                    "./src/templates/blank/",
                    f"./projects/{project_name}/",
                )
            elif template == "Engine Override (basic)":
                shutil.copytree(
                    "./src/templates/engine_override/",
                    f"./projects/{project_name}/",
                )
            elif template == "Engine Injection (advanced)":
                shutil.copytree(
                    "./src/templates/engine_injection/",
                    f"./projects/{project_name}/",
                )
            self.populateProjectList()
            self.projectViewer = ProjectViewer(project_name, self)
            self.projectViewer.show()


class ProjectViewer(QWidget):
    def __init__(self, project_name: str, Mgr: ProjectManager, parent=None):
        super().__init__(parent)
        from PySide6.QtWidgets import (
            QSplitter,
            QHBoxLayout,
            QLabel,
            QPlainTextEdit,
            QListWidgetItem,
            QStackedWidget,
            QScrollArea,
            QTreeWidget,
            QTreeWidgetItem,
            QToolBar,
            QFileDialog,
            QSlider,
        )
        from PySide6.QtGui import (
            QSyntaxHighlighter,
            QTextCharFormat,
            QColor,
            QFont,
            QImage,
            QPixmap,
            QFontDatabase,
        )
        from PySide6.QtGui import QAction
        from PySide6.QtGui import QGuiApplication
        from PySide6.QtGui import QIcon
        from PySide6.QtCore import QByteArray
        from PySide6.QtGui import QImageReader

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

        self.project_name = project_name
        self.Mgr = Mgr
        self.Mgr.hide()
        self.setWindowTitle(f"Project: {self.project_name}")
        self.layout = QVBoxLayout(self)

        # Top bar with actions
        topBar = QHBoxLayout()
        self.buildButton = QPushButton("Build Project", self)
        self.saveButton = QPushButton("Save", self)
        self.saveButton.setDisabled(True)
        self.currentFileLabel = QLabel("No file opened", self)
        self.currentFileLabel.setStyleSheet("color: #666;")
        topBar.addWidget(self.buildButton)
        topBar.addWidget(self.saveButton)
        topBar.addStretch(1)
        topBar.addWidget(self.currentFileLabel)
        self.layout.addLayout(topBar)

        # Splitter: left file lists, right editor
        splitter = QSplitter(self)
        self.layout.addWidget(splitter)

        # Left pane
        leftPane = QWidget(self)
        leftLayout = QVBoxLayout(leftPane)
        leftLayout.setContentsMargins(0, 0, 0, 0)

        leftLayout.addWidget(QLabel("Source files (/Src):", self))
        self.srcList = QListWidget(self)
        leftLayout.addWidget(self.srcList, 1)

        leftLayout.addWidget(QLabel("Assets (/assets):", self))
        self.assetList = QListWidget(self)
        leftLayout.addWidget(self.assetList, 1)

        splitter.addWidget(leftPane)

        # Right pane (stacked viewers: editor, image, archive, font, media, info)
        rightPane = QWidget(self)
        rightLayout = QVBoxLayout(rightPane)
        rightLayout.setContentsMargins(0, 0, 0, 0)

        self.viewerStack = QStackedWidget(self)
        rightLayout.addWidget(self.viewerStack)

        # 0 - Text editor (for code/text files)
        self.editor = QPlainTextEdit(self)
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
        self.viewerStack.addWidget(self.imageScroll)

        # 2 - Archive browser (zip/tar)
        self.archivePane = QWidget(self)
        from PySide6.QtWidgets import QVBoxLayout as _QVBox

        _al = _QVBox(self.archivePane)
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
        _fl = _QVBox(self.fontPane)
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
        _ml = _QVBox(self.mediaPane)
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

        splitter.addWidget(rightPane)
        splitter.setStretchFactor(1, 1)

        # Syntax highlighter for C++
        self.highlighter = CppHighlighter(self.editor.document())

        # State
        self.project_root = os.path.abspath(os.path.join("projects", self.project_name))
        self.currentFilePath = None
        self._dirty = False

        # Signals
        self.buildButton.clicked.connect(self.buildProject)
        self.saveButton.clicked.connect(self.saveCurrentFile)
        self.srcList.itemDoubleClicked.connect(self._openFromList)
        self.assetList.itemDoubleClicked.connect(self._openFromList)
        self.actionExtract.triggered.connect(self._extractArchive)
        self.fontSizeSlider.valueChanged.connect(self._updateFontSampleSize)
        self.actionPlayPause.triggered.connect(self._togglePlayPause)

        # Populate lists
        self.populateLists()

        # Media state
        self._player = None
        self._audioOut = None

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
        super().closeEvent(e)

    def populateLists(self):
        self.srcList.clear()
        self.assetList.clear()

        # Src files (filter to common C/C++ extensions)
        src_dir = os.path.join(self.project_root, "Src")
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
        for root, _, files in os.walk(src_dir) if os.path.isdir(src_dir) else []:
            for f in files:
                if os.path.splitext(f)[1].lower() in code_ext:
                    rel = os.path.relpath(os.path.join(root, f), self.project_root)
                    self.srcList.addItem(rel)

        # Assets (all files)
        assets_dir = os.path.join(self.project_root, "assets")
        for root, _, files in os.walk(assets_dir) if os.path.isdir(assets_dir) else []:
            for f in files:
                rel = os.path.relpath(os.path.join(root, f), self.project_root)
                self.assetList.addItem(rel)

        self.srcList.sortItems()
        self.assetList.sortItems()

    def _openFromList(self, item):
        rel_path = item.text()
        abs_path = os.path.join(self.project_root, rel_path)
        self.openFile(abs_path)

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
        import struct
        from PySide6.QtGui import QImageReader

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
        build_project(self.project_name, open_in_android_studio=True)

    # -------- View helpers --------
    def _setView(self, idx):
        self.viewerStack.setCurrentIndex(idx)
        # Save button only enabled for text editor
        self.saveButton.setEnabled(idx == 0 and self.currentFilePath is not None)

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
        from PySide6.QtGui import QImageReader, QPixmap

        reader = QImageReader(path)
        reader.setAutoTransform(True)
        img = reader.read()
        if img.isNull():
            self._showInfo(path, {"type": "image", "error": reader.errorString()})
            return
        pix = QPixmap.fromImage(img)
        self.imageLabel.setPixmap(pix)
        self.imageLabel.adjustSize()
        self._setView(1)

    def _showArchive(self, path, subtype):
        import zipfile, tarfile, os
        from PySide6.QtWidgets import QTreeWidgetItem

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
        from PySide6.QtWidgets import QFileDialog

        dest = QFileDialog.getExistingDirectory(self, "Extract to…", self.project_root)
        if not dest:
            return
        import zipfile, tarfile

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
        from PySide6.QtGui import QFontDatabase, QFont

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
        from PySide6.QtCore import QUrl

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
        from PySide6.QtMultimedia import QMediaPlayer

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
