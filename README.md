# gemOS
To run
```
docker pull hritwik567/gem5-x86
docker run -t -p 9090:3456 -d hritwik567/gem5-x86
docker exec -it <container-id> /bin/bash
```
And in a seperate terminal window 
```
telnet 0.0.0.0 9090
```
