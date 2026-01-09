QLCV - He thong quan ly cong viec

Ung dung quan ly du an: tao du an, quan ly task, xem Gantt, chat.

================================================================================
CAI DAT
================================================================================

Ubuntu/Debian:

sudo apt update
sudo apt install -y build-essential git sqlite3 libsqlite3-dev pkg-config libgtk-3-dev libgdk-pixbuf2.0-dev

macOS:

/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
echo 'eval "$(/opt/homebrew/bin/brew shellenv)"' >> ~/.zprofile
eval "$(/opt/homebrew/bin/brew shellenv)"
brew update
brew install git sqlite gtk+3 gdk-pixbuf pkg-config

Clone project:

git clone https://github.com/hopite601/QLCV.git
cd QLCV

Build server:

cd server
make 


Build client:
cd client
make 

================================================================================
CHAY CHUONG TRINH
================================================================================

Terminal 1 - Chay server:

cd server
./server

(Giu terminal nay chay, dung tat)

Terminal 2 - Chay client (mo terminal moi - neu muon test them nhieu nguoi thi mo them nhieu terminal khac):

cd client
./gtk_client

================================================================================
HUONG DAN SU DUNG
================================================================================

1. Dang ky: Nhan Register de tao user moi
2. Dang nhap: Login voi user vua tao
3. Projects tab: Tao project, moi member
4. Tasks tab: Tao task voi Assignee, Start/End date (YYYY-MM-DD), update status/progress
5. Gantt tab: Xem timeline task
6. Chat tab: Chat theo project

================================================================================
RESET DU LIEU
================================================================================

Dung server (Ctrl + C), sau do:

cd QLCV/server
rm data.db
./server

Database se duoc tao moi.

================================================================================
GHI CHU
================================================================================

- Tren Windows: Nen dung WSL (Windows Subsystem for Linux) voi Ubuntu
- Doi SERVER_PORT trong common.h: Phai build lai ca server va client
- Loi ket noi: Kiem tra server co chay chua va cong ket noi co khop khong
