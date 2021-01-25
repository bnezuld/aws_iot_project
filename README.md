# aws_iot_project
 aws iot project base for cc3220sf
 
 edit BASE_DIR_ROOT in project properties->resources->linked resources(or in .project) to point to local amazon-freertos  
 
 run these command to make sure priave keys and ssid and password are not saved  
 git update-index --skip-worktree demos/include/aws_clientcredential.h  
 git update-index --skip-worktree demos/include/aws_clientcredential_keys.h  
 