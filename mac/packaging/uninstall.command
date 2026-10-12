#!/bin/bash
# Remove minSolo. Double-click in Finder.
echo "This removes /Applications/minSolo.app and its settings."
pkill -x minSolo 2>/dev/null || true
sudo rm -rf "/Applications/minSolo.app"
defaults delete com.bialkowski.minsolo 2>/dev/null || true
sudo pkgutil --forget com.bialkowski.minsolo 2>/dev/null || true
echo "Done. The System Audio Recording permission can be removed in System Settings > Privacy & Security."
