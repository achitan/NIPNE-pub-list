This package takes care of updating a list of publications for researchers.

login to lxplus

git clone https://github.com/achitan/NIPNE-pub-list.git

cd ..../NIPNE-pub-list

source /cvmfs/sft.cern.ch/lcg/views/LCG_106/x86_64-el9-gcc13-opt/setup.sh

LCG_DIR=/cvmfs/sft.cern.ch/lcg/views/LCG_106/x86_64-el9-gcc13-opt

g++ -O3 -std=c++17 NIPNE-pub-list.cpp -o NIPNE-pub-list -L"$LCG_DIR/lib" -L"$LCG_DIR/lib64" -Wl,-rpath,"$LCG_DIR/lib:$LCG_DIR/lib64" -lcurl -ljsoncpp -lboost_program_options -pthread

./NIPNE-pub-list -a "<your_name_on_inspirehep>" -u "<your_uname_on_nipne>" -p "<your_pass_on_nipne>"

