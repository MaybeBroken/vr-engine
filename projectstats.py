"""Project Statistics Viewer - refactored into a responsive graphical viewer.

This viewer performs background filesystem scanning and streams incremental
statistics to the UI so it can handle very large repositories.
"""

import sys
import os
import heapq
import time
from collections import Counter
from pathlib import Path

from PySide6.QtWidgets import (
    QApplication,
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QLineEdit,
    QFileDialog,
    QProgressBar,
    QTableWidget,
    QTableWidgetItem,
    QHeaderView,
    QSizePolicy,
)
from PySide6.QtCore import Qt, QThread, Signal

# Optional: matplotlib for charts (comes from requirements.txt)
from matplotlib.backends.backend_qtagg import FigureCanvasQTAgg as FigureCanvas
import matplotlib.pyplot as plt


def sizeof_fmt(num: int) -> str:
    for unit in ["B", "KB", "MB", "GB", "TB"]:
        if abs(num) < 1024.0:
            return f"{num:3.1f} {unit}"
        num /= 1024.0
    return f"{num:.1f} PB"


class ScannerThread(QThread):
    """Scans a directory tree in a background thread and emits incremental stats.

    Instead of relying on a fixed list of file extensions, this thread will
    attempt to count lines for any file that appears to be text. This is
    determined lazily by inspecting small chunks and skipping obviously binary
    data. This keeps behavior flexible while still being safe for large trees.
    """

    progress = Signal(dict)  # emits partial stats snapshots
    finished = Signal(dict)  # final stats
    status = Signal(str)

    def __init__(self, root: str, top_n: int = 20):
        super().__init__()
        self.root = Path(root)
        self._stop_requested = False
        self.top_n = top_n

    def stop(self):
        self._stop_requested = True

    def run(self):
        start_time = time.time()
        file_count = 0
        dir_count = 0
        total_size = 0
        total_lines = 0
        # number of text lines per extension (language)
        lines_per_ext = Counter()
        # number of files per extension
        ext_counter = Counter()

        # full list of files for pagination: (size, lines, ext, path)
        files = []

        # Walk directory iteratively to avoid recursion limit and large memory.
        for dirpath, dirnames, filenames in os.walk(self.root):
            if self._stop_requested:
                self.status.emit("Stopped by user")
                break
            dir_count += 1
            for fname in filenames:
                if self._stop_requested:
                    break
                file_count += 1
                try:
                    fpath = Path(dirpath) / fname
                    stat = fpath.stat()
                    fsize = stat.st_size
                    total_size += fsize
                    # Normalize extension; empty string becomes "(no ext)" later
                    ext = fpath.suffix.lower()
                    ext_counter[ext] += 1

                    # default to 0 lines until we successfully count
                    lines = 0

                    # count lines for files that appear to be text (streaming, safe)
                    try:
                        with open(fpath, "rb") as fh:
                            # Peek at a small prefix to decide if it's likely text.
                            prefix = fh.read(1024)
                            if b"\0" in prefix:
                                # likely binary; skip line counting to avoid overhead
                                pass
                            else:
                                # Include prefix in line counting, then continue.
                                lines = prefix.count(b"\n")
                                for chunk in iter(lambda: fh.read(8192), b""):
                                    lines += chunk.count(b"\n")
                                total_lines += lines
                                lines_per_ext[ext] += lines
                    except Exception:
                        # ignore unreadable files
                        pass

                    # record file for UI pagination (even if binary)
                    files.append((fsize, lines, ext, str(fpath)))

                except Exception:
                    # ignore files we can't stat
                    continue

                # Emit progress periodically to keep UI responsive (every 500 files)
                if file_count % 500 == 0:
                    snapshot = {
                        "files": file_count,
                        "dirs": dir_count,
                        "size": total_size,
                        "lines": total_lines,
                        "exts": dict(ext_counter),
                        "lines_per_ext": dict(lines_per_ext),
                        "file_list": files,
                        "elapsed": time.time() - start_time,
                    }
                    self.progress.emit(snapshot)

        # final emit
        final = {
            "files": file_count,
            "dirs": dir_count,
            "size": total_size,
            "lines": total_lines,
            "exts": dict(ext_counter),
            "lines_per_ext": dict(lines_per_ext),
            "file_list": files,
            "elapsed": time.time() - start_time,
        }
        self.finished.emit(final)


class ProjectStatsViewer(QWidget):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Project Statistics Viewer")
        self.setGeometry(100, 100, 1000, 700)

        self.scanner = None

        # pagination state
        self.current_files = []  # list of (size, lines, ext, path)
        self.page_size = 100
        self.current_page = 0

        main_layout = QVBoxLayout(self)

        # Top control row
        controls = QHBoxLayout()
        self.path_edit = QLineEdit(str(Path.cwd()))
        self.browse_btn = QPushButton("Browse")
        self.start_btn = QPushButton("Start Scan")
        self.stop_btn = QPushButton("Stop")
        self.stop_btn.setEnabled(False)

        controls.addWidget(QLabel("Path:"))
        controls.addWidget(self.path_edit)
        controls.addWidget(self.browse_btn)
        controls.addWidget(self.start_btn)
        controls.addWidget(self.stop_btn)

        main_layout.addLayout(controls)

        # Status and progress
        status_layout = QHBoxLayout()
        self.status_label = QLabel("Idle")
        self.progress_bar = QProgressBar()
        self.progress_bar.setRange(0, 0)  # indeterminate until we have a target
        self.progress_bar.setTextVisible(False)
        status_layout.addWidget(self.status_label)
        status_layout.addWidget(self.progress_bar)
        main_layout.addLayout(status_layout)

        # Middle layout: table + chart
        middle = QHBoxLayout()

        # File table (paged)
        self.table = QTableWidget(0, 4)
        self.table.setHorizontalHeaderLabels(["Size", "Lines", "Ext", "Path"])
        self.table.horizontalHeader().setSectionResizeMode(
            0, QHeaderView.ResizeToContents
        )
        self.table.horizontalHeader().setSectionResizeMode(
            1, QHeaderView.ResizeToContents
        )
        self.table.horizontalHeader().setSectionResizeMode(
            2, QHeaderView.ResizeToContents
        )
        self.table.horizontalHeader().setSectionResizeMode(3, QHeaderView.Stretch)
        self.table.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        middle.addWidget(self.table, 2)

        # Chart area (matplotlib)
        self.figure, self.ax = plt.subplots(figsize=(4, 4))
        self.canvas = FigureCanvas(self.figure)
        middle.addWidget(self.canvas, 1)

        main_layout.addLayout(middle)

        # Pagination controls + summary
        bottom = QHBoxLayout()
        self.prev_btn = QPushButton("Prev")
        self.next_btn = QPushButton("Next")
        self.page_label = QLabel("Page 1/1")
        self.summary_label = QLabel("Files: 0 | Dirs: 0 | Size: 0 B | Lines: 0")

        self.prev_btn.setEnabled(False)
        self.next_btn.setEnabled(False)

        bottom.addWidget(self.prev_btn)
        bottom.addWidget(self.next_btn)
        bottom.addWidget(self.page_label)
        bottom.addStretch(1)
        bottom.addWidget(self.summary_label)
        main_layout.addLayout(bottom)

        # Connect signals
        self.browse_btn.clicked.connect(self.on_browse)
        self.start_btn.clicked.connect(self.on_start)
        self.stop_btn.clicked.connect(self.on_stop)
        self.prev_btn.clicked.connect(self.on_prev_page)
        self.next_btn.clicked.connect(self.on_next_page)

    def on_browse(self):
        path = QFileDialog.getExistingDirectory(
            self, "Select Project Directory", str(Path.cwd())
        )
        if path:
            self.path_edit.setText(path)

    def on_start(self):
        root = self.path_edit.text().strip() or str(Path.cwd())
        if not os.path.exists(root):
            self.status_label.setText("Path does not exist")
            return

        # disable/enable controls
        self.start_btn.setEnabled(False)
        self.stop_btn.setEnabled(True)
        self.status_label.setText("Scanning...")
        self.progress_bar.setRange(0, 0)

        self.table.setRowCount(0)
        self.ax.clear()
        self.canvas.draw()

        # start scanner thread
        self.scanner = ScannerThread(root, top_n=50)
        self.scanner.progress.connect(self.on_progress)
        self.scanner.finished.connect(self.on_finished)
        self.scanner.status.connect(self.on_status)
        self.scanner.start()

    def on_stop(self):
        if self.scanner:
            self.scanner.stop()
            self.status_label.setText("Stopping...")
            self.stop_btn.setEnabled(False)

    def on_progress(self, snapshot: dict):
        # update UI with incremental snapshot
        files = snapshot.get("files", 0)
        dirs = snapshot.get("dirs", 0)
        size = snapshot.get("size", 0)
        lines = snapshot.get("lines", 0)
        exts = snapshot.get("exts", {})
        lines_per_ext = snapshot.get("lines_per_ext", {})
        file_list = snapshot.get("file_list", [])

        self.summary_label.setText(
            f"Files: {files} | Dirs: {dirs} | Size: {sizeof_fmt(size)} | Lines: {lines}"
        )
        self.status_label.setText(f"Scanning... processed {files} files")

        # update file list & table (paged)
        self.current_files = file_list
        self._update_pagination()

        # update chart: lines per language (top 12)
        # use lines_per_ext; fallback to counts if empty
        data_counter = Counter(lines_per_ext or exts)
        items = data_counter.most_common(12)
        labels = [k if k else "(no ext)" for k, _ in items]
        vals = [v for _, v in items]
        self.ax.clear()
        if vals:
            self.ax.pie(vals, labels=labels, autopct="%1.1f%%")
        self.ax.set_title("Lines per language (top)")
        self.canvas.draw()

    def on_finished(self, final: dict):
        self.on_progress(final)
        self.status_label.setText("Finished")
        self.progress_bar.setRange(0, 1)
        self.progress_bar.setValue(1)
        self.start_btn.setEnabled(True)
        self.stop_btn.setEnabled(False)

    def on_status(self, text: str):
        self.status_label.setText(text)

    # Pagination helpers
    def _update_pagination(self):
        total = len(self.current_files)
        if total == 0:
            self.table.setRowCount(0)
            self.prev_btn.setEnabled(False)
            self.next_btn.setEnabled(False)
            self.page_label.setText("Page 1/1")
            return

        max_page = max(0, (total - 1) // self.page_size)
        self.current_page = max(0, min(self.current_page, max_page))

        start = self.current_page * self.page_size
        end = min(start + self.page_size, total)
        page_items = self.current_files[start:end]

        self.table.setRowCount(len(page_items))
        for row, (size, lines, ext, path) in enumerate(page_items):
            size_item = QTableWidgetItem(sizeof_fmt(size))
            size_item.setTextAlignment(Qt.AlignRight | Qt.AlignVCenter)

            lines_item = QTableWidgetItem(str(lines))
            lines_item.setTextAlignment(Qt.AlignRight | Qt.AlignVCenter)

            ext_label = ext if ext else "(no ext)"
            ext_item = QTableWidgetItem(ext_label)

            path_item = QTableWidgetItem(path)

            self.table.setItem(row, 0, size_item)
            self.table.setItem(row, 1, lines_item)
            self.table.setItem(row, 2, ext_item)
            self.table.setItem(row, 3, path_item)

        self.prev_btn.setEnabled(self.current_page > 0)
        self.next_btn.setEnabled(self.current_page < max_page)
        self.page_label.setText(f"Page {self.current_page + 1}/{max_page + 1}")

    def on_prev_page(self):
        if self.current_page > 0:
            self.current_page -= 1
            self._update_pagination()

    def on_next_page(self):
        total = len(self.current_files)
        if total == 0:
            return
        max_page = max(0, (total - 1) // self.page_size)
        if self.current_page < max_page:
            self.current_page += 1
            self._update_pagination()


def main():
    app = QApplication(sys.argv)
    viewer = ProjectStatsViewer()
    viewer.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
