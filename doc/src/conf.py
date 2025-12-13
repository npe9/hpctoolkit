# SPDX-FileCopyrightText: Contributors to the HPCToolkit Project
#
# SPDX-License-Identifier: Apache-2.0

# pylint: disable=fixme,invalid-name

"Sphinx configuration for this documentation tree"

import os
import re
import time
from importlib import import_module
from pathlib import Path, PurePath
from typing import Dict, Optional, Tuple

# Mapping from PyPI package names to Python import names
PKG_IMPORT_NAMES = {
    "myst-parser": "myst_parser",
    "sphinx-book-theme": "sphinx_book_theme",
}

# Sphinx "extensions" that need to be checked directly, e.g. HTML themes
KNOWN_NON_EXTENSIONS = {"sphinx_book_theme"}


def path_to_url(path: PurePath) -> str:
    "Convert a Path into a suitable URL for HTML purposes"
    return ("/" if path.is_absolute() else "") + "/".join(path.parts[1:])


def abs_url(base: str, path: str) -> str:
    "Generate the absolute URL given the base"
    path = path.lstrip("/")
    if base and not base.endswith("/"):
        return f"{base}/{path}"
    return (base or "/") + path


def parse_requirements_txt(
    requirements_txt: Path,
) -> Tuple[Optional[str], Dict[str, str]]:
    "Parse a requirements.txt formatted file and convert to Sphinx needs_* values"
    sphinx_ver: Optional[str] = None
    ext_vers: Dict[str, str] = {}

    for spec in {
        line.split("#", maxsplit=1)[0].strip()
        for line in requirements_txt.read_text(encoding="utf-8")
        .replace("\\\n", "")
        .splitlines()
    } - {""}:
        match = re.fullmatch(r"(\S+)\s*>=(\d+\.\d+)", spec)
        if not match:
            raise ValueError(f"Unsupported specifier: {spec!r}")
        if match[1] == "Sphinx":
            if sphinx_ver is not None:
                raise ValueError("requirements.in specifies Sphinx multiple times")
            sphinx_ver = match[2]
        else:
            pkg = PKG_IMPORT_NAMES[match[1]]
            if pkg in ext_vers:
                raise ValueError(f"requirements.in specifies {pkg} multiple times")
            ext_vers[pkg] = match[2]

    # Remove any known non-extensions and check them directly
    for ext in KNOWN_NON_EXTENSIONS:
        if ext in ext_vers:
            ext_module = import_module(ext)
            if ext_vers[ext] > ext_module.__version__:
                raise RuntimeError(
                    f"Version of {ext} is too old, need >={ext_vers[ext]} but have {ext_module.__version__}"
                )
            del ext_vers[ext]

    return sphinx_ver, ext_vers


MIN_VALID_MTIME = time.strptime("1980-01-01 00:00:00", "%Y-%m-%d %H:%M:%S")
MIN_VALID_MTIME_SEC = time.mktime(MIN_VALID_MTIME)


def maybe_fix_file_timestamps() -> None:
    """
    Fix the modified timestamp on the given file to be within a valid range.

    All known versions of Sphinx are currently affected by a bug where if static source
    files have a modified time before 1980, the EPUB3Builder crashes.
    See https://github.com/sphinx-doc/sphinx/issues/14168 for details.

    Due to this bug, we need to fix the modified time for all affected source files.
    """

    def fix(file: Path) -> None:
        try:
            file_stat = file.lstat()
            mtime = time.localtime(file_stat.st_mtime)
            if mtime < MIN_VALID_MTIME:
                print(
                    f"conf.py: Adjusting faulty modified time on {file} (was {time.strftime('%a, %d %b %Y %H:%M:%S +0000', mtime)})"
                )
                os.utime(
                    file,
                    times=(file_stat.st_atime, MIN_VALID_MTIME_SEC),
                    follow_symlinks=False,
                )
        except OSError:
            pass

    for file in html_static_path:
        fix(Path(__file__).parent / Path(file))
    for parent, subdirs, files in os.walk(Path(__file__).parent):
        for subdir in subdirs:
            path = Path(parent) / subdir
            fix(path)
        for fn in files:
            path = Path(parent) / fn
            if path.suffix in (".svg", ".png", ".jpg", ".gif"):
                fix(path)


# Project settings
project = "HPCToolkit"
assert project.lower() == os.environ["CONF_PROJECT_NAME"]
project_copyright = "HPCToolkit Project a Series of LF Projects, LLC"
author = f"The {project} Developers"
release = os.environ["CONF_VERSION"]
version = release

suppress_warnings = [
    # FIXME: The EPUB builder tries to include ALL files in the build directory, not just
    # the ones produced by Sphinx. This tends to cause warnings when the files aren't of a
    # supported MIME type. For now, ignore this kind of warning entirely.
    "epub.unknown_project_files",
]

# Minimum required versions are parsed from the sibling requirements.in
needs_sphinx, needs_extensions = parse_requirements_txt(
    Path(__file__).parent / "requirements.in"
)

# Configuration for the sources, which are primarily written in MyST
language = "en"
master_doc = "index"
extensions = ["myst_parser"]
myst_enable_extensions = [
    "deflist",
    "replacements",
    "smartquotes",
]


# Configuration for the HTML output
html_theme = "sphinx_book_theme"
html_title = "HPCToolkit"
html_logo = "branding/logo.svg"
html_favicon = "branding/favicon.svg"
html_baseurl = os.environ.get("CONF_HTML_BASEURL") or path_to_url(
    PurePath(os.environ["CONF_INSTALL_PREFIX"])
)
html_static_path = ["branding/hpcviewer.svg"]
html_sidebars = {
    "**": [
        "navbar-logo",
        "icon-links",
        "search-button-field",
        "sbt-sidebar-nav",
    ]
}
html_theme_options: dict = {
    "icon_links": [
        {
            "name": "HPCViewer",
            "url": abs_url(html_baseurl, "users/hpcviewer/hpcviewer.html"),
            "type": "local",
            "icon": "_static/hpcviewer.svg",
        },
        {
            "name": "Download as EPUB",
            "url": abs_url(html_baseurl, project.lower() + ".epub"),
            "type": "fontawesome",
            "icon": "fa-solid fa-book",
        },
    ],
    "repository_url": "https://gitlab.com/hpctoolkit/hpctoolkit",
    "repository_branch": "develop",
    "path_to_docs": "doc/src/",
    "use_edit_page_button": True,
    "use_source_button": True,
    "use_issues_button": True,
}

if os.environ["CONF_HAS_PDF"] == "true":
    html_theme_options["icon_links"].append(  # type: ignore
        {
            "name": "Download as PDF",
            "url": abs_url(html_baseurl, project.lower() + ".pdf"),
            "type": "fontawesome",
            "icon": "fa-regular fa-file-pdf",
        }
    )

if os.environ["CONF_VERSIONS_URL"]:
    html_theme_options["switcher"] = {
        "version_match": release,
        "json_url": os.environ["CONF_VERSIONS_URL"],
    }
    html_theme_options["show_version_warning_banner"] = True
    html_sidebars["**"].append("version-switcher")


# Configuration for the EPUB output
epub_basename = project.lower()


# Before leaving and starting the build, fix file timestamps that need to be fixed.
maybe_fix_file_timestamps()
