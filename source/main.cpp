#include "../include/MyoCpp.hpp"
#include <fann.h>
#include <chrono>
#include <cstdlib>


void EventWorker(int event);

int main(int argc, char** argv)
{
	if(argc<2) {
		std::cout << "Usage: <indragon> [port]" << std::endl;
		return 0;
	}
	std::string myoPort(argv[1]);
	MyoCpp::MyoRaw myo(myoPort);
	
	std::recursive_mutex mutex;
	
	std::vector<float> emgData(8,0.0);
	std::vector<float> imuData(10,0.0);
			
	int mode = 0; //0=input, 1=tainGesture, 2=readGesture
	int toTrain = 0;
	
	std::thread worker([&toTrain,&mutex,&mode,&emgData,&imuData](void) {
		//NNetwork n(18,1);
		int toStore = 2; //dont  possible
		std::vector<float> historicalData(toStore*18,0.0);
		fann *n = fann_create_from_file("data.dat");
		if(n==NULL) {
			n = fann_create_standard(3,18,2000,10);
		}
		fann_set_activation_function_hidden(n, FANN_SIGMOID_SYMMETRIC);
		//fann_set_training_algorithm(n,FANN_TRAIN_QUICKPROP);
		fann_set_learning_rate(n,0.1);
		while(mode!=99) {
			std::this_thread::sleep_for(std::chrono::milliseconds(17));
			mutex.lock();
			std::vector<float> in = emgData;	
			in.insert(in.end(), imuData.begin(), imuData.end());
			
			//std::cout << std::endl;
			historicalData.insert(historicalData.begin(),in.begin(),in.end());
			historicalData.resize(18*toStore);
			mutex.unlock();
			//clamp 
			int i = 0;
			for(;i<8;i++) {
				in[i] = in[i]*.001;
			}
			for(;i<12;i++) {
				in[i] = in[i]*.0001;
			}
			for(;i<15;i++) {
				in[i] = in[i]*0.001;
			}
			for(;i<18;i++) {
				in[i] = in[i]/360.0;
			}
			/*for(auto s : in) {
				std::cout << s << ",";
			}
			std::cout << std::endl;*/	
			switch(mode) {
			case 0:
				break;

			case 1:
				{
					std::vector<float> expectedOutput(10,0.0);
					//for(int i=0;i<toTrain;i++) {
						expectedOutput[toTrain] = 1.0;
					//}
						
					std::cout << "Output should be:  (";
					for(auto e : expectedOutput) {
						std::cout << e << " , ";
					}
					std::cout << ")" << std::endl; 
					fann_train(n, in.data(), expectedOutput.data());
					
				}
			case 2:
				{			
					float *d = fann_run(n,in.data());
					std::cout << "Input resulted in: (";
					for(int i=0;i<10;i++) {
						std::cout << d[i] << " , ";
					}	
					std::cout <<")" << std::endl;
					float biggest = 0.0;
					int b =0;
					for(int i=0;i<10;i++) {
						if(d[i]>biggest) {
							biggest=d[i];
							b=i;
						}
					}	
					std::cout << "DETECTED: " << b << std::endl;
					if(mode!=1) {
						EventWorker(b);
					} 
				break;
				}
			case 3:
				fann_save(n,"data.dat");
				mode = 2;
				break;
			default:
				break;
			}
			
		}
	});
	myo.AddEmgHandler([&emgData,&mutex](std::vector<unsigned short> data) {
		mutex.lock();
		emgData.clear();
		for(short d : data) {
			emgData.push_back((float)(d));
		}
		mutex.unlock();
	});
	myo.AddImuHandler([&imuData,&mutex](std::vector<short> data) {
		mutex.lock();
		imuData.clear();
		for(short d : data) {
			imuData.push_back((float)(d));
		}
		mutex.unlock();
	});

			
	std::vector<double> trainOut(10,0.0);
	
	while(mode!=99) {
		int in;
		std::cin >> in;
		if(in==toTrain) {
			mode = 3;
			toTrain = 0;
		}
		else if(in>0&&in>9) {
			mode = in;
		}
		else {
			mode = 1;
			toTrain=in;
		}
	}
	worker.join();
	return 0;
}

void EventWorker(int event)
{
	static int lastEvent = 0;
	float minEventTime = 0.4f;
	static auto tBeg = std::chrono::high_resolution_clock::now();
		if(lastEvent!=event && std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - tBeg).count() > minEventTime) {
			tBeg = std::chrono::high_resolution_clock::now();
			lastEvent = event;
			std::cout << "RAISED EVENT: " << event << std::endl << std::endl;
			if(event==5) {
				system("xdotool key XF86AudioMute");

			}	
	}
}
