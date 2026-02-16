# Contributing to Drone Calibration

Thank you for your interest in contributing to the Drone Calibration project! This document provides guidelines and instructions for contributing to the project.

## Table of Contents

- [Prerequisites](#prerequisites)
- [Local Setup](#local-setup)
- [Development Workflow](#development-workflow)
- [Running CI Locally](#running-ci-locally)
- [Code Quality](#code-quality)
- [Contribution Process](#contribution-process)
- [Code Review](#code-review)
- [Reporting Bugs](#reporting-bugs)
- [Getting Help](#getting-help)

## Prerequisites

### Required Software

- **Qt 6.10.0 or later** (with MinGW 13.1.0 on Windows)
  - Modules required: Qt Widgets, Qt OpenGL, Qt Multimedia, Qt Network
- **CMake 3.20+**
- **Ninja Build System**
- **C++17 Compatible Compiler**
  - Windows: MinGW-w64 13.1.0 (included with Qt)
  - Linux: GCC 9+ or Clang 10+
- **Git** for version control
- **OpenGL 3.3** support (for 3D visualization)

### Optional Tools

- **Qt Creator** - Recommended IDE for Qt development
- **Visual Studio Code** - Alternative editor with CMake/C++ extensions
- **clang-format** - For code formatting (optional but recommended)

## Local Setup

### 1. Clone the Repository

```bash
git clone https://github.com/OptiTrack/Drone-Calibration.git
cd Drone-Calibration
```

### 2. Install Qt

**Qt Online Installer**

1. Download the Qt Online Installer from [qt.io](https://www.qt.io/download-qt-installer)
2. Install Qt 6.10.0 with the following components:
   - MinGW 13.1.0 64-bit (Windows)
   - Qt Multimedia
   - Qt Network
   - Qt OpenGL Widgets

### 3. Set Up Environment Variables (Windows)

Add the following to your system PATH:
- `C:\Qt\6.10.0\mingw_64\bin`
- `C:\Qt\Tools\mingw1310_64\bin`

Or set them temporarily in your terminal:
```powershell
$env:PATH = "C:\Qt\6.10.0\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;$env:PATH"
```

### 4. Install Build Tools

**Windows:**
```powershell
choco install cmake ninja
```

**Linux:**
```bash
sudo apt-get install cmake ninja-build
```

### 5. Build the Project

Navigate to the Qt Drone UI directory:

```bash
cd qt-drone-ui
mkdir build
cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build .
```

Or use the provided build script (Windows):
```bash
cd qt-drone-ui
.\build.bat
```

### 6. Run the Application

```bash
cd qt-drone-ui/build
./QtDroneUI.exe  # Windows
./QtDroneUI      # Linux
```

Or use the run script (Windows):
```bash
cd qt-drone-ui
.\run.bat
```

## Development Workflow

### Branch Naming Convention

Use descriptive branch names following this pattern:
- `feature/<description>` - For new features
- `fix/<description>` - For bug fixes
- `refactor/<description>` - For code refactoring
- `docs/<description>` - For documentation updates
- `test/<description>` - For test additions/improvements

**Examples:**
- `feature/add-waypoint-import`
- `fix/camera-connection-timeout`
- `refactor/consolidate-network-handlers`
- `docs/update-api-reference`

### Commit Messages

Follow the conventional commits format:

```
<type>: <short summary>

<detailed description (optional)>

<footer (optional)>
```

**Types:**
- `feat:` - New feature
- `fix:` - Bug fix
- `docs:` - Documentation changes
- `refactor:` - Code refactoring
- `test:` - Adding or updating tests
- `chore:` - Maintenance tasks
- `style:` - Code style/formatting changes

**Example:**
```
feat: Add flight path export to JSON

Implement functionality to export flight paths to JSON format
including waypoints, metadata, and coordinate system information.

Closes #42
```

## Running CI Locally

The project uses GitHub Actions for continuous integration. You can verify your changes will pass CI by running the build process locally.

### Build Verification

**Windows:**
```powershell
cd qt-drone-ui
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

**Expected Output:**
- Build completes without errors
- `QtDroneUI.exe` is created in `build/` directory

### Manual Testing Checklist

Before submitting a PR, verify:
- [ ] Application launches without crashes
- [ ] No compile warnings in your changes
- [ ] UI elements render correctly
- [ ] Network connections work (VOXL 2 or simulation mode)
- [ ] 3D waypoint visualization renders properly
- [ ] No memory leaks (use Qt Creator's analyzer)

## Code Quality

### Code Style

- Use **4 spaces** for indentation (no tabs)
- Follow **Qt coding conventions** for C++ code
- Keep lines under **120 characters** where practical
- Use **meaningful variable and function names**
- Add **comments** for complex logic

### File Organization

```
qt-drone-ui/
├── src/
│   ├── controllers/     # Business logic controllers
│   ├── models/          # Data models
│   ├── network/         # Network communication
│   ├── utils/           # Utility functions
│   └── widgets/         # UI widgets
├── docs/                # Documentation
├── paths/               # Sample flight paths
└── CMakeLists.txt       # Build configuration
```

### Code Documentation

- Add **header comments** to all new classes
- Document **public methods** with their parameters and return values
- Use **Doxygen-style comments** for API documentation

**Example:**
```cpp
/**
 * @brief Connects to the VOXL 2 drone via TCP
 * @param ipAddress The IP address of the VOXL 2 (e.g., "192.168.1.10")
 * @param port The TCP port number (default: 5000)
 * @return true if connection successful, false otherwise
 */
bool connectToVoxl(const QString& ipAddress, int port = 5000);
```

## Contribution Process

### 1. Create a Feature Branch

```bash
git checkout -b feature/your-feature-name
```

### 2. Make Your Changes

- Write clean, maintainable code
- Follow the code style guidelines
- Add tests if applicable
- Update documentation as needed

### 3. Test Your Changes

- Build and run the application
- Verify all existing functionality still works
- Test your new feature/fix thoroughly

### 4. Commit Your Changes

```bash
git add .
git commit -m "feat: Add your feature description"
```

### 5. Push to GitHub

```bash
git push -u origin feature/your-feature-name
```

### 6. Open a Pull Request

**PR Title:** Clear and descriptive (50 characters or less)

**PR Description Should Include:**
- **What:** Brief description of the changes
- **Why:** Reason for the changes
- **How:** Implementation approach (if complex)
- **Testing:** How you tested the changes
- **Screenshots:** For UI changes
- **Closes:** Reference to any related issues (e.g., `Closes #42`)

**PR Template Example:**
```markdown
## Description
Brief description of what this PR does.

## Motivation
Why is this change needed? What problem does it solve?

## Changes Made
- Change 1
- Change 2
- Change 3

## Testing
- [ ] Built successfully on Windows
- [ ] Tested in simulation mode
- [ ] Tested with VOXL 2 hardware
- [ ] UI elements display correctly

## Screenshots
[Add screenshots if applicable]

## Related Issues
Closes #XX
```

### Definition of Done (DoD)

A contribution is considered complete when:

- [ ] Code builds without errors or warnings
- [ ] All new code follows project style guidelines
- [ ] Public APIs are documented
- [ ] Changes have been tested manually
- [ ] No existing functionality is broken
- [ ] UI changes are responsive and match design guidelines
- [ ] PR description is complete and clear
- [ ] Code has been reviewed and approved
- [ ] All review comments are addressed
- [ ] CI build passes on GitHub Actions

## Code Review

### Review Process

1. **Automated Checks:** GitHub Actions will automatically build your PR
2. **Peer Review:** At least one team member must review and approve
3. **Testing:** Reviewer will verify functionality as described
4. **Feedback:** Address any comments or requested changes
5. **Approval:** Once approved, your PR will be merged

### Review Expectations

**For Contributors:**
- Respond to review comments within 2 business days
- Be open to feedback and suggestions
- Ask questions if feedback is unclear
- Update your PR based on review comments

**For Reviewers:**
- Provide constructive, specific feedback
- Review within 2 business days of PR submission
- Test functionality when possible
- Approve or request changes clearly

### What Reviewers Look For

- **Correctness:** Does the code work as intended?
- **Quality:** Is the code clean, maintainable, and well-structured?
- **Style:** Does it follow project conventions?
- **Safety:** Are there potential bugs, crashes, or memory leaks?
- **Performance:** Are there any performance concerns?
- **Documentation:** Is the code adequately documented?
- **Testing:** Has it been appropriately tested?

## Reporting Bugs

### Where to Report

Report bugs by creating a new issue on GitHub:
**[GitHub Issues](https://github.com/OptiTrack/Drone-Calibration/issues)**

### Bug Report Template

When reporting bugs, please include:

**Title:** Clear, descriptive summary (e.g., "Camera feed freezes after 5 minutes")

**Description:**
```markdown
## Description
Clear description of the bug

## Steps to Reproduce
1. Step one
2. Step two
3. Step three

## Expected Behavior
What should happen

## Actual Behavior
What actually happens

## Environment
- OS: Windows 11 / Ubuntu 22.04
- Qt Version: 6.10.0
- Build Type: Debug / Release
- Hardware: VOXL 2 / Simulation

## Screenshots/Logs
[Add screenshots or log output if applicable]

## Additional Context
Any other relevant information
```

### Feature Requests

For feature requests or enhancements, use the same GitHub Issues page with:
- Clear description of the proposed feature
- Use case / motivation
- Any implementation ideas (optional)
- Label the issue as `enhancement`

## Getting Help

### Communication Channels

**For Questions:**
- **GitHub Discussions:** [Drone-Calibration Discussions](https://github.com/OptiTrack/Drone-Calibration/discussions)
- **Issues:** For bug reports and feature requests only

**For Development Help:**
- Tag questions with `question` label in GitHub Issues
- Check existing documentation in the `docs/` folder
- Review the project README: [README.md](README.md)

### Point of Contact

For urgent matters or general inquiries:
- **Repository Maintainers:** Check the repository's main page for current maintainers
- **Team Channel:** Contact through your organization's internal communication channel

### Resources

- **Qt Documentation:** [doc.qt.io](https://doc.qt.io/)
- **CMake Documentation:** [cmake.org/documentation](https://cmake.org/documentation/)
- **Project README:** [README.md](qt-drone-ui/README.md)
- **API Reference:** See `docs/` folder for detailed API documentation

---

## Quick Reference

### Common Commands

```bash
# Clone repository
git clone https://github.com/OptiTrack/Drone-Calibration.git

# Create feature branch
git checkout -b feature/my-feature

# Build project
cd qt-drone-ui && mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build .

# Run application
./QtDroneUI.exe

# Update branch with main
git checkout main
git pull
git checkout feature/my-feature
git merge main

# Push changes
git push -u origin feature/my-feature
```

### Remember

- Test your changes thoroughly
- Write clear commit messages
- Keep PRs focused and reasonably sized
- Be respectful and constructive in reviews
- Ask questions when unsure

---

**Thank you for contributing to Drone Calibration!**
