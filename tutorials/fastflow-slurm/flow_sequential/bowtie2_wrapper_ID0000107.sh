mkdir -p $NODE_LOCAL/bowtie2_wrapper_ID0000107
cd $NODE_LOCAL/bowtie2_wrapper_ID0000107
if [ -e $ID0000106/SRR1518625_2.fastq ]; then  /bin/ln -s $ID0000106/SRR1518625_2.fastq ./  ; else scp $ID0000106_node:$ID0000106/SRR1518625_2.fastq ./ ; fi
if [ -e $ID0000001/reference.2.bt2 ]; then  /bin/ln -s $ID0000001/reference.2.bt2 ./  ; else scp $ID0000001_node:$ID0000001/reference.2.bt2 ./ ; fi
if [ -e $ID0000001/reference.rev.1.bt2 ]; then  /bin/ln -s $ID0000001/reference.rev.1.bt2 ./  ; else scp $ID0000001_node:$ID0000001/reference.rev.1.bt2 ./ ; fi
if [ -e $ID0000001/reference.1.bt2 ]; then  /bin/ln -s $ID0000001/reference.1.bt2 ./  ; else scp $ID0000001_node:$ID0000001/reference.1.bt2 ./ ; fi
if [ -e $ID0000001/reference.rev.2.bt2 ]; then  /bin/ln -s $ID0000001/reference.rev.2.bt2 ./  ; else scp $ID0000001_node:$ID0000001/reference.rev.2.bt2 ./ ; fi
if [ -e $ID0000001/reference.4.bt2 ]; then  /bin/ln -s $ID0000001/reference.4.bt2 ./  ; else scp $ID0000001_node:$ID0000001/reference.4.bt2 ./ ; fi
if [ -e $ID0000106/SRR1518625_1.fastq ]; then  /bin/ln -s $ID0000106/SRR1518625_1.fastq ./  ; else scp $ID0000106_node:$ID0000106/SRR1518625_1.fastq ./ ; fi
if [ -e $ID0000001/reference.3.bt2 ]; then  /bin/ln -s $ID0000001/reference.3.bt2 ./  ; else scp $ID0000001_node:$ID0000001/reference.3.bt2 ./ ; fi
date +%s,%H:%M:%S > ${0}.log
start=$(date +%s%3N)
$PFS_PATH/sra-search-workflow/tools/bowtie2_wrapper SRR1518625 
duration=$(( $(date +%s%3N) - start ))
echo ID0000107,bowtie2_wrapper,$duration,$start,$node4 >> $PFS_PATH/sra-search-workflow/192sras_large/16nodes/dfman/stat.log
date +%s,%H:%M:%S >> ${0}.log