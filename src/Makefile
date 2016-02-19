#
# To compile, type "make" 
# To remove files, type "make clean"
#
LIBS = -lpthread -lrt
all: wspace_ap_scout

wspace_ap_scout: wspace_ap_scout.o wspace_asym_util.o time_util.o tun.o  loss_rate_parser.o\
fec.o feedback_records.o monotonic_timer.o rate_adaptation.o sample_rate.o robust_rate.o scout_rate.o
	$(CXX) $(CXXFLAGS) $^ -o wspace_ap_scout $(LIBS)

%.o: %.cc
	$(CXX) $(CXXFLAGS) -o $@ -c $<

clean:
	rm -rf wspace_ap_scout *.o 

tag: 
	ctags -R *
