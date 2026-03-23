#!/bin/bash
set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR" || exit 1

if [ -x "$SCRIPT_DIR/.venv/bin/python" ]; then
  PYTHON_BIN="$SCRIPT_DIR/.venv/bin/python"
elif command -v python3 >/dev/null 2>&1; then
  PYTHON_BIN="$(command -v python3)"
else
  echo "Python 3 introuvable."
  echo "Installe Python 3 puis relance ce fichier."
  read -r -n 1 -p "Appuie sur une touche pour fermer..." _
  echo
  exit 1
fi

if ! "$PYTHON_BIN" -c "import serial" >/dev/null 2>&1; then
  echo "Le module pyserial est manquant."
  echo "Commande conseillée: $PYTHON_BIN -m pip install pyserial"
  read -r -n 1 -p "Appuie sur une touche pour fermer..." _
  echo
  exit 1
fi

"$PYTHON_BIN" "$SCRIPT_DIR/uart_acquisition_ui.py"
STATUS=$?

if [ $STATUS -ne 0 ]; then
  echo
  echo "L'application s'est arrêtée avec le code: $STATUS"
  read -r -n 1 -p "Appuie sur une touche pour fermer..." _
  echo
fi

exit $STATUS
