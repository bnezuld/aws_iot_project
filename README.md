# aws_iot_project
 aws iot project base for cc3220sf
 
 edit BASE_DIR_ROOT in project properties->resources->linked resources(or in .project) to point to local amazon-freertos  
 
 ran these command to make sure priave keys and ssid and password are not saved  
git update-index --assume-unchanged application_code/config/aws_clientcredential.h
git update-index --assume-unchanged application_code/config/aws_clientcredential_keys.h 
 