RUN=00022; docker run --rm -v /home/.rucio.cfg:/home/.rucio.cfg -v /home/daq/data/run${RUN}.mid.gz:/app/run${RUN}.mid.gz \
   gmazzitelli/rucio-uploader:v0.2 --file /app/run${RUN}.mid.gz --bucket cygno-data --did_name WC/run${RUN}.mid.gz \
   --upload_rse CNAF_USERDISK --transfer_rse T1_USERTAPE --account rucio-daq
