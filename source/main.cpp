#include "../include/MyoCpp.hpp"
#include <fann.h>
#include <chrono>

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
		fann *n = fann_create_standard(5,18,4000,2000,100,10);
		fann_set_activation_function_hidden(n, FANN_SIGMOID_SYMMETRIC);
		//fann_set_training_algorithm(n,FANN_TRAIN_QUICKPROP);
		fann_set_learning_rate(n,0.1);
		while(mode!=99) {
			mutex.lock();
			std::vector<float> in = emgData;	
			in.insert(in.end(), imuData.begin(), imuData.end());
			mutex.unlock();
			//clamp 
			int i = 0;
			for(;i<8;i++) {
				in[i] = in[i] *.001;
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
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
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
				break;
				}
			default:
				return;
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
			mode = 2;
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
