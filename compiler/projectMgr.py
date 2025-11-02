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
        )
        from PySide6.QtGui import (
            QSyntaxHighlighter,
            QTextCharFormat,
            QColor,
            QFont,
        )

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

        # Right pane (editor)
        rightPane = QWidget(self)
        rightLayout = QVBoxLayout(rightPane)
        rightLayout.setContentsMargins(0, 0, 0, 0)

        self.editor = QPlainTextEdit(self)
        self.editor.setTabStopDistance(
            4 * self.editor.fontMetrics().horizontalAdvance(" ")
        )
        self.editor.textChanged.connect(self._onTextChanged)
        rightLayout.addWidget(self.editor)

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

        # Populate lists
        self.populateLists()

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
        try:
            with open(path, "rb") as fh:
                chunk = fh.read(2048)
            chunk.decode("utf-8")
            return True
        except Exception:
            return False

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

        if not self._is_text(path):
            QMessageBox.information(
                self, "Open Asset", "Binary asset preview not supported."
            )
            return

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
            self.currentFilePath = path
            self._dirty = False
            self._updateTitle()
            self.saveButton.setEnabled(True)
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


if __name__ == "__main__":

    app = QApplication(sys.argv)
    window = ProjectManager()
    window.show()
    sys.exit(app.exec())
