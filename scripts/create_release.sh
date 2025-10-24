#!/bin/bash
# Simple release script for socket.io-client-cpp
# Usage: ./scripts/create_release.sh

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}Socket.IO C++ Client - Release Tool${NC}\n"

# Check if we're in git repo
if ! git rev-parse --git-dir > /dev/null 2>&1; then
    echo -e "${RED}Error: Not a git repository${NC}"
    exit 1
fi

# Check for uncommitted changes
if [[ -n $(git status -s) ]]; then
    echo -e "${RED}Error: You have uncommitted changes${NC}"
    echo "Please commit or stash them before creating a release"
    git status -s
    exit 1
fi

# Get current version from CMakeLists.txt
CURRENT_VERSION=$(grep "VERSION" CMakeLists.txt | head -1 | awk '{print $2}')
echo -e "Current version: ${YELLOW}$CURRENT_VERSION${NC}"

# Ask for new version
echo -e "\n${GREEN}What type of release is this?${NC}"
echo "1) Major (breaking changes) - X.0.0"
echo "2) Minor (new features) - x.Y.0"
echo "3) Patch (bug fixes) - x.y.Z"
echo "4) Custom version"
read -p "Select (1-4): " RELEASE_TYPE

IFS='.' read -r -a VERSION_PARTS <<< "$CURRENT_VERSION"
MAJOR="${VERSION_PARTS[0]}"
MINOR="${VERSION_PARTS[1]}"
PATCH="${VERSION_PARTS[2]}"

case $RELEASE_TYPE in
    1)
        NEW_VERSION="$((MAJOR + 1)).0.0"
        ;;
    2)
        NEW_VERSION="$MAJOR.$((MINOR + 1)).0"
        ;;
    3)
        NEW_VERSION="$MAJOR.$MINOR.$((PATCH + 1))"
        ;;
    4)
        read -p "Enter version (e.g., 3.2.0): " NEW_VERSION
        ;;
    *)
        echo -e "${RED}Invalid selection${NC}"
        exit 1
        ;;
esac

echo -e "\n${GREEN}New version will be: ${YELLOW}v$NEW_VERSION${NC}"
read -p "Continue? (y/n): " CONFIRM

if [[ $CONFIRM != "y" ]]; then
    echo "Cancelled"
    exit 0
fi

echo -e "\n${GREEN}Step 1: Updating version in files...${NC}"

# Update CMakeLists.txt
sed -i "s/VERSION $CURRENT_VERSION/VERSION $NEW_VERSION/" CMakeLists.txt
echo "✓ Updated CMakeLists.txt"

# Update README.md (if versioned)
if grep -q "Recent Improvements" README.md; then
    sed -i "s/## Recent Improvements/## What's New in v$NEW_VERSION/" README.md
    echo "✓ Updated README.md"
elif grep -q "What's New in v" README.md; then
    sed -i "s/What's New in v[0-9.]\+/What's New in v$NEW_VERSION/" README.md
    echo "✓ Updated README.md"
fi

echo -e "\n${GREEN}Step 2: Building and testing...${NC}"
if [ -d "build" ]; then
    rm -rf build
fi

mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

# Run tests if they exist
if [ -f "test/thread_safety_test" ]; then
    echo "Running tests..."
    ./test/thread_safety_test
    ./test/sio_test
    echo "✓ All tests passed"
fi

cd ..

echo -e "\n${GREEN}Step 3: Committing version bump...${NC}"
git add CMakeLists.txt README.md CHANGELOG.md 2>/dev/null || true
git commit -m "chore: bump version to $NEW_VERSION"
echo "✓ Committed version bump"

echo -e "\n${GREEN}Step 4: Creating git tag...${NC}"
read -p "Enter release notes (or press Enter to skip): " RELEASE_NOTES

if [ -z "$RELEASE_NOTES" ]; then
    git tag -a "v$NEW_VERSION" -m "Release v$NEW_VERSION"
else
    git tag -a "v$NEW_VERSION" -m "Release v$NEW_VERSION

$RELEASE_NOTES"
fi
echo "✓ Created tag v$NEW_VERSION"

echo -e "\n${GREEN}Step 5: Ready to push!${NC}"
echo -e "Run these commands to publish:"
echo -e "  ${YELLOW}git push origin master${NC}"
echo -e "  ${YELLOW}git push origin v$NEW_VERSION${NC}"
echo ""
echo "Then create a GitHub release at:"
echo "  https://github.com/yourusername/socket.io-client-cpp/releases/new?tag=v$NEW_VERSION"

echo -e "\n${GREEN}Release preparation complete!${NC}"
