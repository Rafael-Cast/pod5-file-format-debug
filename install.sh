git submodule update --init --recursive
python -m setuptools_scm
python -m pod5_make_version
cd python/pod5;
make install;
cd ../..
source ./python/pod5/venv/bin/activate
mkdir build
cd build
cmake ..
cd c++/examples
make -j
cd ../..
cd ..
