# SPDX-FileCopyrightText: Contributors to the HPCToolkit Project
#
# SPDX-License-Identifier: Apache-2.0

# We need latexmk to generate a PDF version of the docs. Since we change the sidebar
# based on whether this is available, find it first.
latexmk = find_program(
  'latexmk',
  native: true,
  required: get_option('manual_pdf'),
  disabler: true,
)
manual_f = get_option('manual').enable_if(
  get_option('manual_pdf').enabled(),
  error_message: 'PDF manual cannot be enabled without HTML manual',
)

sphinx_args = ['-q', '--keep-going', '--jobs=auto', '@CURRENT_SOURCE_DIR@']
if get_option('werror')
  sphinx_args += ['-W']
endif

sphinx_rc_args = [
  '-bdummy',
  '-q',
  '--jobs=auto',
  meson.project_source_root() / meson.current_source_dir(),
  meson.project_build_root() / meson.current_build_dir(),
]
sphinx_env = {
  'CONF_PROJECT_NAME': meson.project_name(),
  'CONF_VERSION': meson.project_version(),
  'CONF_INSTALL_PREFIX': get_option('prefix') / docdir,
  'CONF_HTML_BASEURL': get_option('docs_host_url'),
  'CONF_VERSIONS_URL': get_option('docs_versions_url'),
  'CONF_HAS_PDF': latexmk.found() ? 'true' : '',
}

# Find Sphinx for documentation rendering.
sphinx_force_fb = 'sphinx' in get_option('force_fallback_for') or get_option('wrap_mode') == 'forcefallback'
sphinx = disabler()
if not sphinx_force_fb
  sphinx = find_program(
    'sphinx-build',
    version: '>=6.0',
    native: true,
    required: false,
    disabler: true,
  )

  # NB: We also require a number of extensions, not just Sphinx. To make sure Sphinx has
  # everything it needs, we do a "dummy" build that produces no output but confirms all
  # the extensions we want are all available to sphinx-build.
  if sphinx.found()
    sphinx_result = run_command(sphinx, sphinx_rc_args, env: sphinx_env, check: false)
    if sphinx_result.returncode() != 0
      message(
        'Found sphinx-build is missing extensions, disabling (see meson-log.txt for details)',
      )
      sphinx = disabler()
    endif
  endif
endif

# If needed, install a fallback Sphinx. This uses a Python virtual environment with all
# the packages installed via Pip.
if sphinx_force_fb or not sphinx.found() and manual_f.enabled() and get_option(
  'wrap_mode',
) != 'nofallback'
  message('Installing Sphinx + extensions in a virtual environment with Pip...')
  sphinx_venv = meson.project_build_root() / meson.current_build_dir() / '.venv'
  run_command(
    build_python,
    '-m',
    'venv',
    ensurepip_found ? [] : '--without-pip',
    '--upgrade',
    sphinx_venv,
    check: true,
  )
  run_command(
    sphinx_venv / 'bin' / 'python',
    ensurepip_found ? ['-m', 'pip'] : pip,
    '--no-input',
    '--disable-pip-version-check',
    '--require-virtualenv',
    'install',
    '--no-warn-script-location',
    '--require-hashes',
    get_option('wrap_mode') == 'nodownload' ? '--no-index' : [],
    '--requirement',
    files('requirements.txt'),
    check: true,
  )
  sphinx = find_program(sphinx_venv / 'bin' / 'sphinx-build', native: true)

  # Run the "dummy" build to ensure everything is updated like it's supposed to be
  run_command(sphinx, sphinx_rc_args, env: sphinx_env, check: true)
endif

# The main documentation output is static, multi-file HTML
docs_html = custom_target(
  output: 'index.html',
  command: [sphinx, '-bhtml', sphinx_args, '@OUTDIR@'],
  env: sphinx_env,
  console: true,
  build_always_stale: true,
)
meson.add_install_script(
  build_python,
  files('install_html.py'),
  docs_html,
  docdir,
  dry_run: true,
  install_tag: 'docs',
)
if manual_f.enabled() and is_disabler(docs_html)
  error('manual is enabled but is missing dependencies')
endif

# The HTML site can optionally have a version switcher. While we can't generate the
# entire set of versions reliably, we provide the JSON object for this version.
configure_file(
  output: '_version.json',
  input: '_version.json.in',
  configuration: {
    'VERSION': meson.project_version(),
    'URL': get_option('docs_host_url') != '' ? get_option('docs_host_url') : 'file://'
    + (get_option('prefix') / docdir),
  },
  install: true,
  install_tag: 'docs',
  install_dir: docdir,
)

# For offline reading, the recommended format is EPUB. This is essentially the HTML
# site repackaged as a single file.
docs_epub = custom_target(
  output: '@0@.epub'.format(meson.project_name().to_lower()),
  command: [sphinx, '-bepub', sphinx_args, '@OUTDIR@'],
  env: sphinx_env,
  console: true,
  build_always_stale: true,
  install: true,
  install_tag: 'docs',
  install_dir: docdir,
)
if manual_f.enabled() and is_disabler(docs_epub)
  error('manual is enabled but is missing dependencies')
endif

# For offline reading, a very common format is PDF. Sphinx doesn't support PDF output
# directly, instead it can generate Latex which can be rendered as a PDF.
docs_latex = custom_target(
  output: '@0@.tex'.format(meson.project_name().to_lower()),
  command: [sphinx, '-blatex', sphinx_args, '@OUTDIR@'],
  env: sphinx_env,
  console: true,
  build_always_stale: true,
)
docs_pdf = custom_target(
  output: '@0@.pdf'.format(meson.project_name().to_lower()),
  input: docs_latex,
  command: [latexmk, '-cd', '-pdf', '-dvi-', '-ps-', '@INPUT@'],
  console: true,
  build_always_stale: true,
  install: true,
  install_tag: 'docs',
  install_dir: docdir,
)
if get_option('manual_pdf').enabled() and is_disabler(docs_pdf)
  error('manual_pdf is enabled but is missing dependencies')
endif

# Helper target to build all the documentation at once
docs_all = [docs_html, docs_epub]
if not is_disabler(docs_pdf)
  docs_all += docs_pdf
endif
alias_target('docs', docs_all)

# Helper target to serve the built docs over HTTP for development
run_target(
  'docs-serve',
  command: [
    build_python,
    files('serve.py'),
    meson.current_build_dir(),
    get_option('prefix') / docdir,
  ],
  depends: docs_all,
)
