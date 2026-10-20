set -euo pipefail

export HOMEBREW_NO_AUTO_UPDATE=1
export HOMEBREW_NO_INSTALL_UPGRADE=1
export HOMEBREW_NO_INSTALLED_DEPENDENTS_CHECK=1
export HOMEBREW_NO_INSTALL_CLEANUP=1

if ! command -v automake >/dev/null; then
  brew install --build-from-source automake
fi
if ! command -v glibtool >/dev/null; then
  brew install libtool
fi
for tool in nasm ninja pkg-config; do
  if ! command -v "$tool" >/dev/null; then
    brew install "$tool"
  fi
done

tools_venv="$RUNNER_TEMP/ayugram-build-tools"
python3 -m venv "$tools_venv"
"$tools_venv/bin/python3" -m pip install --disable-pip-version-check meson==1.10.0
echo "$tools_venv/bin" >> "$GITHUB_PATH"
export PATH="$tools_venv/bin:$PATH"

for tool in automake glibtool meson nasm ninja pkg-config cmake; do
  command -v "$tool"
  "$tool" --version
done
