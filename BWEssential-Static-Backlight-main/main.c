#include "main.h"

int main(){
	hid_init();

	hid_device *device = find_BWE();
	if(device == NULL){
		printf("Device was not found or could not be opened");
		return 1;
	}
	
	int sent = hid_send_feature_report(device, data, sizeof(data) / sizeof(char));

	hid_close(device);
	
	hid_exit();
}

hid_device *find_BWE(){
	struct hid_device_info *linked_list_devices = hid_enumerate(DEV_VID, DEV_PID);
	struct hid_device_info *device_info = linked_list_devices;
	while(device_info != NULL){
		if(device_info->usage_page == DEV_USAGE_PAGE && device_info->usage == DEV_USAGE && device_info->interface_number == DEV_INTERFACE_NUMBER){
			break;
		}
		device_info = device_info->next;
	}

	if(device_info == NULL){
		hid_free_enumeration(linked_list_devices);
		return NULL;
	}

	hid_device *device = hid_open_path(device_info->path);

	hid_free_enumeration(linked_list_devices); // free the linked list

	return device;
}