#!/bin/bash

# Nome completo del file passato da MIDAS
NAME=$(python3 -c "import midas.client; c=midas.client.MidasClient('midas2rucio'); print(c.odb_get('/Logger/Channels/0/Settings/Current filename'))")
FILEPATH=$(python3 -c "import midas.client; c=midas.client.MidasClient('midas2rucio'); print(c.odb_get('/Logger/Data dir'))")

LOG="/home/daq/DAQ/online/logs/rucio_upload.log"

echo "uploading: $FILEPATH $FILE" >> $LOG
python3 -c "import midas.client, sys; c=midas.client.MidasClient('midas2rucio'); c.msg('INFO: uploading file {:s}'.format(sys.argv[1])); c.disconnect()" "$NAME"
# Esempio: comprimi il file
# gzip ${FILEPATH}${NAME}

docker run --rm -v /home/.rucio.cfg:/home/.rucio.cfg -v ${FILEPATH}${NAME}:/app/${NAME}    gmazzitelli/rucio-uploader:v0.2 --file /app/${NAME} --bucket cygno-data --did_name WC/${NAME}    --upload_rse CNAF_USERDISK --transfer_rse T1_USERTAPE --account rucio-daq >> $LOG
python3 -c "import midas.client, sys; c=midas.client.MidasClient('midas2rucio'); c.msg('INFO: RUCIO upload DONE'); c.disconnect()"
exit 0
