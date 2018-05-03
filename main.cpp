#include <stdio.h>
#include <cstring>
#include <vector>

#include <chrono>
#include <ctime>
#include <thread>

#include <nvml.h>

#define checkError(err) \
if ((err) != NVML_SUCCESS) { \
	printf("ERROR: An error has occurred on line %d. Error code: %d. Terminating!\n", __LINE__, err); \
	nvmlShutdown(); \
	return (err); \
}

struct GPUDevice {
	GPUDevice();
	int init();
	int update();
	void print();
	
	int index;
	std::string name;

	unsigned fanSpeed;
	unsigned power;
	unsigned temperature;
	nvmlUtilization_t utilization;

	nvmlDriverModel_t dmCurrent;
	nvmlDriverModel_t dmPending;

	nvmlMemory_t memory;
	nvmlDevice_t handle;
};

GPUDevice::GPUDevice() {
	//blank
	memset(this, 0, sizeof(GPUDevice));
}

int GPUDevice::init() {
	nvmlReturn_t res;
	res = nvmlDeviceGetHandleByIndex(index, &handle);
	checkError(res);
	char temp[256];
	res = nvmlDeviceGetName(handle, temp, 256);
	checkError(res);

	name = temp;
	const int GPU_NAME_TEXT_FIELD_SIZE = 22;
	if (name.size() != GPU_NAME_TEXT_FIELD_SIZE) {
		if (name.size() < GPU_NAME_TEXT_FIELD_SIZE) {
			while (name.size() != GPU_NAME_TEXT_FIELD_SIZE) {
				name.append(" ");
			}
		}
	}

	res = nvmlDeviceGetDriverModel(handle, &dmCurrent, &dmPending);

	if(res != NVML_SUCCESS) {
		printf("%s[%d] ERROR: Could not obtain Driver Model\n", __FUNCTION__, __LINE__);
	}
	return update();
}

int GPUDevice::update() {
	nvmlReturn_t res;

	res = nvmlDeviceGetFanSpeed(handle, &fanSpeed);
	if (res != NVML_SUCCESS) {
		fanSpeed = unsigned(-1);
		res = NVML_SUCCESS;
	}
	checkError(res);

	res = nvmlDeviceGetMemoryInfo(handle, &memory);
	checkError(res);
	res = nvmlDeviceGetPowerUsage(handle, &power);
	if (res != NVML_SUCCESS) {
		power = unsigned(-1);
		res = NVML_SUCCESS;
	} else {
		//Result is in milliwatts so convert it to watts
		power /= 1000;
	}
	checkError(res);
	
	res = nvmlDeviceGetTemperature(handle, NVML_TEMPERATURE_GPU, &temperature);
	checkError(res);
	
	res = nvmlDeviceGetUtilizationRates(handle, &utilization);
	checkError(res);
}

struct DeviceManager {
public:
	DeviceManager();
	~DeviceManager();
	void print();
	int update();
	bool isValid() const { return valid; }
private:
	int init();
	std::string driverVersion;
	unsigned int numDevices;
	std::vector<GPUDevice> devices;
	bool valid;
};

int DeviceManager::init() {
	nvmlReturn_t err;
	err = nvmlInit();
	if (err == NVML_ERROR_DRIVER_NOT_LOADED) {
		printf("ERROR: NVidia driver is not running. Initialization failed.\n");
	} else if(err == NVML_ERROR_NO_PERMISSION) {
		printf("ERROR: NVML does not have permission to talk to the driver. Initialization failed.\n");
	} else if(err == NVML_ERROR_UNKNOWN) {
		printf("ERROR: NVML encounted an unexpected error during initialization. Initialization failed.\n");
	} else if(err == NVML_SUCCESS) {
		char temp[256];
		err = nvmlSystemGetDriverVersion(temp, 256);
		checkError(err);
		driverVersion = temp;

		err = nvmlDeviceGetCount(&numDevices);
		checkError(err);

		devices.resize(numDevices);
		for (int i = 0; i < numDevices; i++) {
			GPUDevice &d = devices[i];
			d.index = i;
			d.init();
		}
	}
	return 0;
}
DeviceManager::DeviceManager() :
	numDevices(0),
	driverVersion("Unknown"),
	valid(0)
{
	if (0 == init()) {
		valid = 1;
	}
}

DeviceManager::~DeviceManager() {
	nvmlReturn_t err;
	err = nvmlShutdown();
}

int DeviceManager::update() {
	for (auto &d : devices) {
		if (d.update()) {
			valid = 0;
			break;
		}
	}
	return !valid;
}
//NVML_DEVICE_NAME_BUFFER_SIZE
void DeviceManager::print() {
	std::time_t time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	char *hrTime = hrTime = ctime(&time);
	printf ("%s", hrTime);
	printf("+-----------------------------------------------------------------------------+\n");
	printf("|             NVidia driver version: %s       Device count : %2d           |\n", driverVersion.c_str(), numDevices);
	printf("|---------------------------------+---------------+---------------------------+\n");
	printf("| Idx    Name            TCC/WDDM | Memory-usage  | Temp   Fan   Power  Util  |\n");
	printf("+-----------------------------------------------------------------------------+\n");
	for (auto &d : devices) {
		d.print();
	}
	printf("+-----------------------------------------------------------------------------+\n");
}

void GPUDevice::print() {
	std::string driverMode = (dmCurrent == NVML_DRIVER_WDDM) ? "WDDM" : "TCC ";
	char fan[10];
	if (fanSpeed != unsigned(-1)) {
		snprintf(fan, 10, " %3d%%" , fanSpeed);
	} else {
		snprintf(fan, 10, " N/A ");
	}

	char pow[10];
	if (power != unsigned(-1)) {
		snprintf(pow, 10, " %3dW" , power);
	} else {
		snprintf(pow, 10, " N/A ");
	}

	printf("| %2d %s  %s | %5d / %5d | %3dC %s   %s   %3d%% |\n", 
		index, name.c_str(), driverMode.c_str(), 
		memory.used / (1024*1024), memory.total / (1024*1024),
		temperature, fan, pow, utilization.gpu
		);
}


#ifdef _WIN32

#include <Windows.h>
void moveCursorTo( int x, int y ) {
	const HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
	if (handle == INVALID_HANDLE_VALUE) {
		return;
	}
	//Hide the cursor
	const CONSOLE_CURSOR_INFO lpConsoleCursorInfo = {1, FALSE};
	SetConsoleCursorInfo(handle, &lpConsoleCursorInfo);

	const COORD p = { x, y };
	SetConsoleCursorPosition(handle, p );
}

#else //!_WIN32
void moveCursorTo( int x, int y ) {
	printf("\033[%d;%dH", y+1, x+1);
}
#endif //!_WIN32





int main() {
	DeviceManager devMan;
	while(devMan.isValid()) {
		moveCursorTo(0, 0);
		devMan.print();
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		devMan.update();
	}
	return 0;
}
