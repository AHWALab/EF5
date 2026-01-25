#!/bin/bash

################################################################################
#            EF5: ENSEMBLE FRAMEWORK FOR FLASH FLOOD FORECASTING
#                       LINUX COMPILATION SCRIPT
################################################################################
# This script automates the compilation of EF5 on Linux systems
# Features:
#   - Automatic dependency detection and installation
#   - Support for multiple Linux distributions
#   - Color-coded output for better readability
#   - Error handling and user prompts
################################################################################

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

################################################################################
# Function: Print colored messages
################################################################################
print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_step() {
    echo -e "${CYAN}[STEP]${NC} $1"
}

################################################################################
# Function: Print header
################################################################################
print_header() {
    echo -e "${CYAN}"
    echo "================================================================================"
    echo "            EF5: ENSEMBLE FRAMEWORK FOR FLASH FLOOD FORECASTING"
    echo "                          COMPILATION SCRIPT"
    echo "================================================================================"
    echo -e "${NC}"
}

################################################################################
# Function: Detect Linux distribution
################################################################################
detect_distro() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        DISTRO=$ID
        DISTRO_VERSION=$VERSION_ID
        print_info "Detected distribution: $NAME $VERSION_ID"
        return 0
    elif [ -f /etc/redhat-release ]; then
        DISTRO="rhel"
        print_info "Detected distribution: Red Hat Enterprise Linux"
        return 0
    else
        print_error "Unable to detect Linux distribution"
        return 1
    fi
}

################################################################################
# Function: Check if running as root
################################################################################
check_root() {
    if [ "$EUID" -eq 0 ]; then
        return 0
    else
        return 1
    fi
}

################################################################################
# Function: Check if a command exists
################################################################################
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

################################################################################
# Function: Check dependencies
################################################################################
check_dependencies() {
    print_step "Checking dependencies..." >&2
    
    local missing_deps=()
    local deps=("git" "autoconf" "automake" "make" "g++")
    
    for dep in "${deps[@]}"; do
        if ! command_exists "$dep"; then
            missing_deps+=("$dep")
            print_warning "Missing: $dep" >&2
        else
            print_success "Found: $dep" >&2
        fi
    done
    
    # Check for pkg-config (needed to find libgeotiff)
    if ! command_exists pkg-config; then
        missing_deps+=("pkg-config")
        print_warning "Missing: pkg-config" >&2
    else
        print_success "Found: pkg-config" >&2
    fi
    
    # Check for libgeotiff
    if command_exists pkg-config; then
        if ! pkg-config --exists libgeotiff 2>/dev/null; then
            missing_deps+=("libgeotiff")
            print_warning "Missing: libgeotiff development libraries" >&2
        else
            print_success "Found: libgeotiff" >&2
        fi
    fi
    
    echo "${missing_deps[@]}"
}

################################################################################
# Function: Install dependencies based on distribution
################################################################################
install_dependencies() {
    local distro=$1
    
    print_step "Installing dependencies for $distro..."
    
    case $distro in
        ubuntu|debian)
            if check_root; then
                apt-get update
                apt-get install -y git autoconf automake build-essential libgeotiff-dev pkg-config zlib1g-dev
            else
                sudo apt-get update
                sudo apt-get install -y git autoconf automake build-essential libgeotiff-dev pkg-config zlib1g-dev
            fi
            ;;
            
        fedora|rhel|centos|rocky|almalinux)
            if check_root; then
                if command_exists dnf; then
                    dnf install -y git autoconf automake gcc-c++ make libgeotiff-devel pkgconfig zlib-devel
                else
                    yum install -y git autoconf automake gcc-c++ make libgeotiff-devel pkgconfig zlib-devel
                fi
            else
                if command_exists dnf; then
                    sudo dnf install -y git autoconf automake gcc-c++ make libgeotiff-devel pkgconfig zlib-devel
                else
                    sudo yum install -y git autoconf automake gcc-c++ make libgeotiff-devel pkgconfig zlib-devel
                fi
            fi
            ;;
            
        arch|manjaro|cachyos|endeavouros|garuda)
            if check_root; then
                pacman -Sy --noconfirm git autoconf automake gcc make libgeotiff pkgconf zlib
            else
                sudo pacman -Sy --noconfirm git autoconf automake gcc make libgeotiff pkgconf zlib
            fi
            ;;
            
        alpine)
            if check_root; then
                apk update
                apk add git autoconf automake build-base libgeotiff-dev pkgconfig zlib-dev
            else
                sudo apk update
                sudo apk add git autoconf automake build-base libgeotiff-dev pkgconfig zlib-dev
            fi
            ;;
            
        opensuse*|sles)
            if check_root; then
                zypper install -y git autoconf automake gcc-c++ make libgeotiff-devel pkg-config zlib-devel
            else
                sudo zypper install -y git autoconf automake gcc-c++ make libgeotiff-devel pkg-config zlib-devel
            fi
            ;;
            
        *)
            print_error "Unsupported distribution: $distro"
            print_info "Please install the following packages manually:"
            print_info "  - git"
            print_info "  - autoconf"
            print_info "  - automake"
            print_info "  - g++ (C++ compiler)"
            print_info "  - make"
            print_info "  - libgeotiff development libraries"
            print_info "  - pkg-config"
            print_info "  - zlib development libraries"
            return 1
            ;;
    esac
    
    return $?
}

################################################################################
# Function: Run autoreconf
################################################################################
run_autoreconf() {
    print_step "Running autoreconf --force --install..."
    
    if autoreconf --force --install; then
        print_success "autoreconf completed successfully"
        return 0
    else
        print_error "autoreconf failed"
        return 1
    fi
}

################################################################################
# Function: Run configure
################################################################################
run_configure() {
    local install_prefix=$1
    
    print_step "Running configure..."
    
    if [ -n "$install_prefix" ]; then
        print_info "Installation prefix: $install_prefix"
        if ./configure --prefix="$install_prefix"; then
            print_success "configure completed successfully"
            return 0
        else
            print_error "configure failed"
            return 1
        fi
    else
        if ./configure; then
            print_success "configure completed successfully"
            return 0
        else
            print_error "configure failed"
            return 1
        fi
    fi
}

################################################################################
# Function: Run make
################################################################################
run_make() {
    print_step "Compiling EF5 with make..."
    
    # Determine number of cores for parallel compilation
    if command_exists nproc; then
        CORES=$(nproc)
    else
        CORES=1
    fi
    
    print_info "Using $CORES cores for compilation"
    
    if make -j"$CORES" CXXFLAGS="-O3 -fopenmp"; then
        print_success "EF5 compiled successfully"
        return 0
    else
        print_error "Compilation failed"
        return 1
    fi
}

################################################################################
# Function: Main compilation process
################################################################################
compile_ef5() {
    local install_prefix=$1
    local ef5_source_dir="$SCRIPT_DIR"
    
    # Check if we're already in the EF5 source directory
    if [ -f "$SCRIPT_DIR/configure.ac" ]; then
        print_info "Script is in EF5 source directory"
        ef5_source_dir="$SCRIPT_DIR"
    elif [ -f "$SCRIPT_DIR/configure" ]; then
        print_info "Script is in EF5 source directory (configure exists)"
        ef5_source_dir="$SCRIPT_DIR"
    elif [ -d "$SCRIPT_DIR/EF5" ] && [ -f "$SCRIPT_DIR/EF5/configure.ac" ]; then
        print_info "Found EF5 source in subdirectory EF5/"
        ef5_source_dir="$SCRIPT_DIR/EF5"
    else
        print_error "Cannot find EF5 source directory with configure.ac"
        print_error "Searched in: $SCRIPT_DIR and $SCRIPT_DIR/EF5"
        exit 1
    fi
    
    # Change to EF5 source directory
    cd "$ef5_source_dir" || {
        print_error "Failed to change to EF5 source directory: $ef5_source_dir"
        exit 1
    }
    
    print_info "Working directory: $(pwd)"
    
    # Run autoreconf if needed
    if [ ! -f "configure" ] || [ -n "$FORCE_AUTORECONF" ]; then
        run_autoreconf || exit 1
    else
        print_info "configure script exists, skipping autoreconf (use -a to force)"
    fi
    
    # Run configure
    run_configure "$install_prefix" || exit 1
    
    # Run make
    run_make || exit 1
    
    # Check if binary was created
    if [ -f "bin/ef5" ]; then
        print_success "EF5 binary created at: $(pwd)/bin/ef5"
    else
        print_warning "EF5 binary not found at expected location (bin/ef5)"
    fi
}

################################################################################
# Function: Display usage
################################################################################
usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -p, --prefix PATH      Set custom installation prefix"
    echo "  -a, --autoreconf       Force running autoreconf"
    echo "  -s, --skip-deps        Skip dependency check and installation"
    echo "  -h, --help             Display this help message"
    echo ""
    echo "Example:"
    echo "  $0                     # Compile with default settings"
    echo "  $0 -p /usr/local       # Compile with custom prefix"
    echo "  $0 -a                  # Force autoreconf before compilation"
    echo ""
}

################################################################################
# Main Script
################################################################################
main() {
    local install_prefix=""
    local skip_deps=false
    
    # Parse command line arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            -p|--prefix)
                install_prefix="$2"
                shift 2
                ;;
            -a|--autoreconf)
                FORCE_AUTORECONF=1
                shift
                ;;
            -s|--skip-deps)
                skip_deps=true
                shift
                ;;
            -h|--help)
                usage
                exit 0
                ;;
            *)
                print_error "Unknown option: $1"
                usage
                exit 1
                ;;
        esac
    done
    
    print_header
    
    # Detect distribution
    if ! $skip_deps; then
        detect_distro || exit 1
        
        # Check dependencies
        missing_deps=($(check_dependencies))
        
        if [ ${#missing_deps[@]} -gt 0 ]; then
            print_warning "Missing dependencies detected: ${missing_deps[*]}"
            echo ""
            read -p "Would you like to install missing dependencies? (y/N): " -n 1 -r
            echo ""
            
            if [[ $REPLY =~ ^[Yy]$ ]]; then
                install_dependencies "$DISTRO" || {
                    print_error "Failed to install dependencies"
                    exit 1
                }
                print_success "All dependencies installed"
            else
                print_warning "Proceeding without installing dependencies"
                print_warning "Compilation may fail if dependencies are missing"
            fi
        else
            print_success "All dependencies are satisfied"
        fi
        echo ""
    fi
    
    # Compile EF5
    compile_ef5 "$install_prefix"
    
    echo ""
    print_success "EF5 compilation process completed!"
    print_info "You can now run EF5 using: $(pwd)/bin/ef5 <control_file>"
}

# Run main function
main "$@"
