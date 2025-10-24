# Release Checklist

Quick reference for creating a new release.

## Pre-Release Checklist

- [ ] All features are complete and tested
- [ ] All tests pass (`./build/test/thread_safety_test` and `./build/test/sio_test`)
- [ ] Documentation is updated
- [ ] No uncommitted changes (`git status`)
- [ ] On master branch (`git branch`)

## Release Steps

### Option 1: Automated (Recommended)

```bash
./scripts/create_release.sh
```

Follow the prompts!

### Option 2: Manual

#### 1. Update Version Number

Edit [CMakeLists.txt](CMakeLists.txt) line 4:

```cmake
PROJECT(sioclient
    VERSION 3.2.0  # ← Update this
)
```

#### 2. Update CHANGELOG.md

Add new section at the top:

```markdown
# [3.2.0](https://github.com/yourusername/socket.io-client-cpp/compare/3.1.0...3.2.0) (2025-10-24)

### Features

- List your new features

### Bug Fixes

- List your bug fixes

### Performance

- List performance improvements
```

#### 3. Update README.md

Change "Recent Improvements" to "What's New in v3.2.0"

#### 4. Build and Test

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
./test/thread_safety_test
./test/sio_test
cd ..
```

#### 5. Commit Version Bump

```bash
git add CMakeLists.txt CHANGELOG.md README.md
git commit -m "chore: bump version to 3.2.0"
```

#### 6. Create Git Tag

```bash
git tag -a v3.2.0 -m "Release v3.2.0

Major improvements:
- C++20 coroutine support
- Detailed disconnect tracking
- Performance optimizations"
```

#### 7. Push to GitHub

```bash
git push origin master
git push origin v3.2.0
```

#### 8. Create GitHub Release

1. Go to: https://github.com/yourusername/socket.io-client-cpp/releases/new
2. Select tag: `v3.2.0`
3. Title: `v3.2.0 - C++20 Coroutines & Enhanced Features`
4. Description: Copy from CHANGELOG.md
5. Click "Publish release"

## Post-Release Checklist

- [ ] GitHub release created
- [ ] Release notes are clear and complete
- [ ] Tag is pushed to repository
- [ ] Users notified (if applicable)
- [ ] Update project README badges (if needed)

## Version Numbering Guide

Follow [Semantic Versioning](https://semver.org/):

- **Major (X.0.0)**: Breaking changes

  - Changed API signatures
  - Removed features
  - Incompatible behavior changes

- **Minor (x.Y.0)**: New features (backward compatible)

  - New APIs
  - New functionality
  - Deprecations (but not removals)

- **Patch (x.y.Z)**: Bug fixes only
  - No new features
  - No API changes
  - Only fixes

## Examples

**Current**: v3.1.0

**Scenarios**:

- Added C++20 coroutines → v3.2.0 (Minor - new feature)
- Fixed memory leak → v3.1.1 (Patch - bug fix)
- Changed emit() signature → v4.0.0 (Major - breaking)

## Quick Commands

```bash
# Check current version
grep VERSION CMakeLists.txt | head -1

# Check git tags
git tag -l

# Check latest tag
git describe --tags --abbrev=0

# Build and test
mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j$(nproc) && ./test/thread_safety_test && ./test/sio_test

# Create release (automated)
./scripts/create_release.sh

# Push everything
git push origin master && git push origin --tags
```

## Troubleshooting

### "Tests are failing"

Fix the tests before releasing. Never release with failing tests.

### "Forgot to update CHANGELOG"

```bash
# Remove the tag
git tag -d v3.2.0

# Update CHANGELOG.md
# Then re-commit and re-tag
git add CHANGELOG.md
git commit --amend
git tag -a v3.2.0 -m "Release v3.2.0"
```

### "Need to update release after publishing"

Edit the GitHub release page directly - you can update the description anytime.

### "Made a mistake in the version number"

```bash
# Delete local tag
git tag -d v3.2.0

# Delete remote tag (if already pushed)
git push origin :refs/tags/v3.2.0

# Fix version in files
# Re-commit and re-tag
```
