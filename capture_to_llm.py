#!/usr/bin/env python3
"""
Capture repository contents (respecting .gitignore) and concatenate to LLM.md
for use by web-based language models.
"""

import os
import pathspec
from pathlib import Path


def load_gitignore(repo_root):
    """Load and parse .gitignore patterns."""
    patterns = []
    gitignore_path = os.path.join(repo_root, '.gitignore')

    if os.path.exists(gitignore_path):
        with open(gitignore_path, 'r') as f:
            patterns = f.read().splitlines()

    # Also check for .gitignore in subdirectories
    for root, dirs, files in os.walk(repo_root):
        if '.gitignore' in files and root != repo_root:
            gitignore_path = os.path.join(root, '.gitignore')
            with open(gitignore_path, 'r') as f:
                subdir_patterns = f.read().splitlines()
                patterns.extend(subdir_patterns)

    # Always exclude .git directory and specified subdirectories
    patterns.append('.git')
    patterns.append('.gitignore')
    patterns.append('plan/**')
    patterns.append('out/**')
    patterns.append('example/**')

    return pathspec.PathSpec.from_lines('gitwildmatch', patterns)


def should_include_file(file_path, repo_root, spec):
    """Check if a file should be included based on .gitignore rules."""
    relative_path = os.path.relpath(file_path, repo_root)
    return not spec.match_file(relative_path)


def capture_repository(repo_root, output_file='LLM.md'):
    """Capture repository contents and write to LLM.md."""

    # Load gitignore patterns
    spec = load_gitignore(repo_root)

    # Collect all files to process
    files_to_process = []

    for root, dirs, files in os.walk(repo_root):
        # Skip .git directory
        dirs[:] = [d for d in dirs if d != '.git']

        for file in sorted(files):
            file_path = os.path.join(root, file)

            if should_include_file(file_path, repo_root, spec):
                files_to_process.append(file_path)

    # Write to LLM.md
    output_path = os.path.join(repo_root, output_file)

    with open(output_path, 'w', encoding='utf-8') as out_f:
        out_f.write('# Repository Contents\n\n')
        out_f.write('This file contains the concatenated contents of the repository.\n\n')
        out_f.write('---\n\n')

        for file_path in sorted(files_to_process):
            relative_path = os.path.relpath(file_path, repo_root)

            # Skip the output file itself
            if relative_path == output_file:
                continue

            out_f.write(f'## File: {relative_path}\n\n')
            out_f.write('```\n')

            try:
                with open(file_path, 'r', encoding='utf-8', errors='replace') as in_f:
                    content = in_f.read()
                    out_f.write(content)
            except Exception as e:
                out_f.write(f'[Error reading file: {e}]\n')

            out_f.write('\n```\n\n')
            out_f.write('---\n\n')

    print(f'Successfully captured {len(files_to_process)} files to {output_path}')


if __name__ == '__main__':
    repo_root = os.path.dirname(os.path.abspath(__file__))
    capture_repository(repo_root)
