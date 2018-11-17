# gemOS
To run
```
docker pull hritwik567/gem5-x86
docker run --privileged -t -p 9090:3456 -d hritwik567/gem5-x86:v3
docker exec -it <container-id> /bin/bash
```
Do all the things mentioned in `gemos-howto.pdf` and then in a seperate terminal window 
```
telnet 0.0.0.0 9090
```
