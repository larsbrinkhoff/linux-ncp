install_ubuntu() {
    sudo apt-get update -ym
    sudo apt-get install -y gcc make screen
}

install_macos() {
    brew update
    brew install gcc make screen
}

case `uname` in
    Linux) install_ubuntu;;
    Darwin) install_macos;;
    *) echo "Unknown operating system"; exit 1;;
esac
