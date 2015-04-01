//File: MyoCpp.hpp: header for the myoCpp library
//Author: Malte "mkalte" Kießling

#include <iostream>
#include <asio.hpp>
#include <string>
#include <vector>
#include <map>
#include <queue>
#include <thread>
#include <mutex>

//namespace: MyoCpp
//note: Holds the MyoCpp classes and funcitons
namespace MyoCpp {
	
	//class: BTPacket
	//note: Represents the packet that we'll send 
	class BTPacket 
	{
	public:
		//constructor: BTPacket
		BTPacket() 
			: typ(0),cls(0),cmd(0)
		{
		}
		
		//constructor: BTPacket
		BTPacket(unsigned char typ, unsigned char cls, unsigned char cmd, std::vector<unsigned char> payload) 
			: typ(typ), cls(cls), cmd(cmd), payload(payload)
		{
		}
		
		//destructor: ~BTPacket
		~BTPacket()
		{
		}
		
		unsigned char typ;
		unsigned char cls;
		unsigned char cmd;
		std::vector<unsigned char> payload;
	};

	typedef std::function<void(BTPacket)> HandlerFunc;
	
	template<class T>
	inline std::vector<unsigned char> pack(T in) {
		std::vector<unsigned char> ret;
		for(unsigned int i=0;i<sizeof(T);i++) {
			unsigned char c = *((unsigned char*)&in+i);
			ret.push_back(c);
		}
		return ret;
	}
	
	template<class T>
	inline std::vector<T> unpak(std::vector<unsigned char> in,unsigned int num, unsigned int beginPos=0) 
	{
		std::vector<T> ret;
		if(sizeof(T)*num <= beginPos+in.size()) {
			for(int i = 0;i<num;i++) {
				T tmp = *reinterpret_cast<T*>(&(in[beginPos+i*sizeof(T)]));
				ret.push_back(tmp);
			}
		}
		return ret;	
	}
	
	template<class T>	
	std::vector<T> vecCat(std::vector<T> v1, std::vector<T> v2) 
	{
		std::vector<T> ret = v1;
		ret.insert(ret.end(), v2.begin(), v2.end());	
		return ret;
	}

	template<class T>
	void packCat(T i, std::vector<unsigned char>&v ) 
	{
		v = vecCat(v,pack<T>(i));
	}	
	
	class OneCallHandler 
	{	
	public:
		unsigned char cls;
		unsigned char cmd;
		HandlerFunc f;
	};

 
	//class: BT 
	//note: Does the communication with the myo-dongle 
	class BT 
	{
	public:
		BT() 
			: isRunning(false), ioService(), port(ioService)
		{

		}
		BT(std::string portname)
			: isRunning(false), ioService(), port(ioService)
		{
			Open(portname);
		}
		
		void Open(std::string portname) 
		{
			port.open(portname);
			port.set_option(asio::serial_port::baud_rate(9600));
			isRunning = true;
			sendThread = std::thread(&BT::SendThread, this);
			recvThread = std::thread(&BT::RecvThread, this);	
		}
		
		~BT()
		{
			isRunning = false;
			sendThread.join();
			recvThread.join();
		}

		void SendThread()
		{
			while(isRunning) {
				if(sendMutex.try_lock()) {
					while(!toSend.empty()) {
						unsigned char c = toSend.front();
						port.write_some(asio::buffer(&c,1));
						toSend.pop();					
					}
					
					sendMutex.unlock();
				}
			}
		}

		void RecvThread()
		{
			std::vector<char> curRecv;
			unsigned int length=0;
			while(isRunning) {
				unsigned char c;
				port.read_some(asio::buffer(&c,1));
				if(curRecv.size()==0 && (c==0x00||c==0x80||c==0x08||c==0x88)) {
					curRecv.push_back(c);
				}
				else if (curRecv.size()==1) {
					curRecv.push_back(c);
					length = 4 + (curRecv[0]&0x07) + curRecv[1];
				}
				else {
					curRecv.push_back(c);
				}
				if(length!=0 && curRecv.size()>=length) {
					std::vector<unsigned char> payload(curRecv.begin()+4, curRecv.end());
					BTPacket p(curRecv[0], curRecv[2], curRecv[3], payload);
					recvMutex.lock();
					recvPackets.push(p);
					recvMutex.unlock();
					length=0;
					curRecv.clear();
					if(p.typ==0x0 && responseHandler) {
						responseHandler(p);
						recvMutex.lock();
						responseHandler = HandlerFunc();
						recvMutex.unlock();
					}
					else if(p.typ==0x80) {
						for(auto f : eventHandlers) {
							if(f) {
								f(p);
							}
						}
						std::vector<HandlerFunc> toCall;	
						for(auto i = specialEventHandlers.begin();i!=specialEventHandlers.end();i++) {
							if((*i).cmd == p.cmd && (*i).cls == p.cls) {
								if((*i).f) {
									toCall.push_back((*i).f);
								}
								//recvMutex.lock();
								i = specialEventHandlers.erase(i);
							
								if(i==specialEventHandlers.end()) {
									break;
								}
							}
						}
						for(auto f : toCall) {
							f(p);
						}
					}
				}
				
			}
		}

		void SendCommand(unsigned char cls, unsigned char cmd, std::vector<unsigned char> payload, HandlerFunc handler=HandlerFunc()) 
		{
			unsigned char length = static_cast<unsigned char>(payload.size());
			std::vector<unsigned char> d;
			d.push_back(0);
			d.push_back(length);
			d.push_back(cls);
			d.push_back(cmd);
			d.insert(d.end(),payload.begin(), payload.end());
			if(handler) {
				SetResponseHandler(handler);
			}
			SendData(d);
		}

		void SendData(std::vector<unsigned char> data) 
		{
			sendMutex.lock();
			for(auto c : data) {
				toSend.push(c);
			}
			sendMutex.unlock();
		}
		
		bool PopPacket(BTPacket&p) 
		{
			if(!recvPackets.empty()) {
				recvMutex.lock();
				p = recvPackets.front();
				recvPackets.pop();
				recvMutex.unlock();
				return true;
			}
			return false;
		}
		void SetResponseHandler(HandlerFunc f) 
		{
			recvMutex.lock();
			responseHandler = f;
			recvMutex.unlock();
		}

		void AddEventHandler(HandlerFunc f)
		{
			eventHandlers.push_back(f);	
		}
		
		void AddSpecialEventHandler(unsigned char cls, unsigned char cmd, HandlerFunc f)
		{
			OneCallHandler h;
			h.cmd = cmd;
			h.cls = cls;
			h.f = f;
			specialEventHandlers.push_back(h);
		}
		
		void Connect(std::vector<unsigned char> addr, HandlerFunc f=HandlerFunc()) 
		{
			std::vector<unsigned char> payload = addr;
			//std::cout << "ass: " << addr.size() << std::endl;
			payload.push_back(0);
			packCat<unsigned short>(6,payload);
			packCat<unsigned short>(6,payload);	
			packCat<unsigned short>(64,payload);
			packCat<unsigned short>(0,payload);
			SendCommand(6,3,payload,f);
		} 

		void GetConnections(HandlerFunc f)
		{
			SendCommand(0,6,std::vector<unsigned char>(),f);
		}
		
		void Discover(HandlerFunc f=HandlerFunc())
		{
			std::vector<unsigned char> payload;
			payload.push_back(1);
			SendCommand(6,2,payload,f);
		}

		void EndScan(HandlerFunc f=HandlerFunc())
		{
			SendCommand(6,4, std::vector<unsigned char>(),f);
		}

		void Disconnect(unsigned char connectionId, HandlerFunc f=HandlerFunc())
		{
			std::vector<unsigned char> payload;
			payload.push_back(connectionId);
			SendCommand(0,3,payload,f);
		}

		void ReadAttrib(unsigned char conn, short attrib, HandlerFunc f)
		{
			std::vector<unsigned char> payload;
			payload.push_back(conn);
			packCat<unsigned short>(attrib,payload);
			AddSpecialEventHandler(4,5,f);
			SendCommand(4,4,payload,HandlerFunc());
			
		}

		void WriteAttrib(unsigned char conn, short attrib, std::vector<unsigned char> val,HandlerFunc f=HandlerFunc())
		{
			std::vector<unsigned char> payload;
			payload.push_back(conn);
			packCat<unsigned short>(attrib, payload);
			payload.push_back(static_cast<unsigned char>(val.size()));
			payload = vecCat(payload,val);
			AddSpecialEventHandler(4,1,f);
			SendCommand(4,5,payload,HandlerFunc());
		}
 
	private:
		bool isRunning;
		
		asio::io_service	ioService;
		asio::serial_port	port;
		
		std::queue<unsigned char>		toSend;
		std::queue<BTPacket>			recvPackets;

		std::thread				sendThread;
		std::recursive_mutex 	sendMutex;
		std::thread				recvThread;
		std::recursive_mutex	recvMutex;

		std::vector<HandlerFunc>	eventHandlers;
		HandlerFunc					responseHandler;
		std::vector<OneCallHandler> specialEventHandlers;
	};
	

	typedef std::function<void(std::vector<unsigned short>)> EmgEventFunc;
	typedef std::function<void(std::vector<short>)> ImuEventFunc;
	//class: MyoRaw
	//note: Does the actual communication with the myo 
	class MyoRaw
	{
	public:
		MyoRaw() 
			: connId(0), bt(), oldProtocol(false)
		{

		}
		MyoRaw(std::string port) 
			: connId(0), bt(), oldProtocol(false)
		{
			Connect(port);
		}

		void Connect(std::string port)
		{
				bt.Open(port);
				std::cout << "Scanning for a myo... " << std::endl;
				bt.Disconnect(0);
				bt.Disconnect(1);
				bt.Disconnect(2);
				bt.Discover();
				MyoCpp::BTPacket p;
				std::string addrString;
				while(true) {
					if(bt.PopPacket(p)) {
						std::string pstring(p.payload.begin(), p.payload.end());
						if(pstring.find("\x06\x42\x48\x12\x4A\x7F\x2C\x48\x47\xB9\xDE\x04\xA9\x01\x00\x06\xD5") != pstring.npos) {
							addrString = pstring.substr(2,6);
			
							break;
						}
					}
				}
			char c;
			bt.EndScan();
			std::vector<unsigned char> addr(addrString.begin(), addrString.end());
			std::cout << "Connecting..." << std::endl;
			bt.AddSpecialEventHandler(3,0,[this](BTPacket p) {this->_StatusOk(p);});
			bt.Connect(addr, [this](BTPacket p) {this->_ConnectionResponse(p);});
		}

		void AddEmgHandler(EmgEventFunc f) 
		{
			emgHandlers.push_back(f);
		}

		void AddImuHandler(ImuEventFunc f)
		{
			imuHandlers.push_back(f);
		}
	private:
		unsigned char 	connId = 0;
		BT 				bt;
		bool			oldProtocol;
		std::vector<EmgEventFunc> emgHandlers;
		std::vector<ImuEventFunc> imuHandlers;

	protected:
		void _ConnectionResponse(BTPacket p) 
		{	
			connId = p.payload[p.payload.size()-1];
			std::cout << "Connected with id: " << static_cast<int>(connId) << std::endl;	
		}
		void _StatusOk(BTPacket p) 
		{
			std::cout << "Getting Firmware version..." << std::endl;
			bt.ReadAttrib(connId,0x17,[this](BTPacket p) { this->_FirmwareResponse(p);});	
		}

		void _FirmwareResponse(BTPacket p)
		{
			std::cout << "Firmware version: ";
			std::vector<unsigned short> v = unpak<unsigned short>(p.payload,4,5);
			if(v.size()==4) {
				std::cout << v[0] << "." << v[1] << "." << v[2] << "." << v[3] << std::endl;
			} else {
				std::cout << "Error!" << std::endl;
			}
			if(v[0]<1) {
				oldProtocol = true;
			}
			//currently only supporting the old protocol 
			if(oldProtocol) {
				std::vector<unsigned char> data;
				//these do unknon stuff says my source
				std::cout << "sending magic..." << std::endl;
				data.push_back(1);data.push_back(2);data.push_back(0);data.push_back(0);
				bt.WriteAttrib(connId,0x19, data, [this](BTPacket p) {
					std::vector<unsigned char>data;
					data.push_back(1);
					data.push_back(0);
					bt.WriteAttrib(connId,0x2f,data, [this,data](BTPacket p) { 
						bt.WriteAttrib(connId,0x2c,data, [this,data](BTPacket p) {
							bt.WriteAttrib(connId,0x32,data, [this,data](BTPacket p) {
								bt.WriteAttrib(connId,0x35,data, [this,data](BTPacket p) {
									bt.WriteAttrib(connId,0x28,data,[this](BTPacket p) {this->_EMG_Ok(p);});
								});
							});
						});
					});
				});
				//enables emg and imu
			
			
			}
		}
		void _EMG_Ok (BTPacket p) {
				std::cout << "donte. engabling emg and imu..:" << std::endl;
				std::vector<unsigned char> data;
				data.push_back(1);
				data.push_back(0);
				bt.WriteAttrib(connId,0x1d,data,[this](BTPacket p) {this->_IMU_Ok(p);});
		}
		void _IMU_Ok (BTPacket p) {
				std::vector<unsigned char> data;
				data.push_back(2);
				data.push_back(9);
				data.push_back(2);
				data.push_back(1);
				packCat<unsigned short>(1000,data);	//sampling rate
				data.push_back(100); //low pass
				data.push_back(1000 / 50);
				data.push_back(50); //sampling rate imu
				data.push_back(0);data.push_back(0);
				bt.WriteAttrib(connId,0x19,data);
				bt.AddEventHandler([this](BTPacket p) { this->HandleEvent(p);});
				std::cout << "Init Done!" << std::endl;
		}

		void HandleEvent(BTPacket p) {
			if(p.cls!=4 || p.cmd!=5) {
				return;
			}

			unsigned short attrib = 0;
			auto a = unpak<unsigned short>(p.payload,1,1);
			std::vector<unsigned short> emgVals;	
			std::vector<short> imuVals;
			if(a.size()!=1) {
				return;
			}
			attrib = a[0];
			switch(attrib) {
			case 0x27:
				emgVals = unpak<unsigned short>(p.payload,8,5);
				if(emgVals.size()==8) {
					for(auto f : emgHandlers) {
						f(emgVals);
					}
				}
			break;
			
			case 0x1c:
				imuVals = unpak<short>(p.payload,10,5);
				if(imuVals.size()==10) {
					for(auto f : imuHandlers) {
						f(imuVals);
					}
				}
				break;
			}
		}
	};
};
