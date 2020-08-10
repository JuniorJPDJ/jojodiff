#!/bin/bash
# filelist for lst-option
orglst=("synch.0000.tar" "synch.0005.tar" "synch1.tar" "synch3.tar" "tstA1.txt" "tstB1.txt")
newlst=("synch.0001.tar" "synch.0006.tar" "synch2.tar" "synch4.tar" "tstA2.txt" "tstB2.txt")

# allopts
optlst=("-vv -bb" "-vv --better" "-vv" "-vv --lazy" "-vv -ff" \
  "-vv --buffer-size 0" "-vv -m 65565" "-vv --index-size 8" "-vv -i 800" \
  "--verbose -v --search-size 1" "-vv -a 1024" \
  "-vv -n 1 -x 2" "-vv --search-min 128 --search-max 256" \
  "-vv --block-size 0" "-vv -k 65565" \
  "-vv --sequential-source" "-vv --sequential-dest" "-vvpq" \
  "-vvck 1024")

# default options
optdef=("-vv -bb" "-vv -b -s" "-vv" "-vv -s" "-vv -f" "-vv -ff")

# pairwise testing
optpw1=("-v" "-vv" "-vvv" "-v -c" "-s" "-a 783 -v" "-v -a 6248")   # 7, must be prime !
optpw3=("" "-b" "-bb" "-f" "-ff")                                  # 5, must be prime !
optpw2=("" "-k 0" "-k 8192" "-k 7311")
optpw4=("" "-n 1 -x 2" "-n 16 -x 3123" )
optpw5=("" "-m 0" "-m 7" "-m 37" "-p" "-q" "-p -q")


if [[ $# -lt 3 ]]
then
    echo 'usage: jtst <jdiff-exe> <jpatch-exe> [bat] [lst | all | fil | tar] <dir> [allopts | defs | stats | pw | <opts1> <opts2> ...]'
    echo "  w32=generate cmd-file for windows"
    echo "  lst=predefined list: ${orglst[@]}"
    echo "  all=all files in dir"
    echo "  fil=files with extention .fil"
    echo "  fil=files with extention .tar"
    exit
fi
jdiff=$1; shift
jpatch=$1; shift
xdelta=$(which xdelta3)
[[ -n "$xdelta" ]] && xdelta="$xdelta -e -s"

# Unfinished Part
  typ="unx"
  if [[ "$1" == "bat" ]]
  then
    echo "Creating bat-file..."
    jdiff="echo >%1"
    jpatch="echo >%2"
    xdelta=":"
    typ="bat"
    shift
  fi

  TEMP=${TEMP:-/tmp}/jtst$$.tmp

# Very Old Part
  if [[ "$1" == "lst" ]]
  then
    shift
    dir=$1; shift

    for ((ind=0;ind<${#orglst[@]};ind++))
    do
      for((optind=0;optind<${#optlst[@]};optind++))
      do
        org=${orglst[ind]}
        new=${newlst[ind]}
        opt=${optlst[optind]}
        dif=${new%.*}-$optind.jdf

        echo "------------------------------------------------------------"
        echo "jdiff $opt $org $new | $jpatch $org - | cmp $new"
        time $jdiff $* $opt $dir/$org $dir/$new $dir/$dif
        if $($jpatch $dir/$org $dir/$dif | cmp $dir/$new); then echo ok; else echo nok; fi
      done
    done 2>&1
  fi

# New Part
  if [[ "$1" == "all" || "$1" == "fil" || "$1" == "tar" ]]
  then
    ext="*.$1"
    if [[ "$1" == "all" ]]; then ext="*"; fi
    shift
    dir=$1; shift

    if [[ "$1" == "allopts" ]]
    then
      shift
    elif [[ "$1" == "defs" ]]
    then
      optlst=("${optdef[@]}")
    elif [[ "$1" == "stats" ]]
    then
      optlst=("-b" " " "-f" "-ff"
        "-b  -i 1" "-b  -i 8" "-b  -i 64" "-b  -i 256" "-b  -i 512"  \
          "    -i 1" "    -i 8" "    -i 64" "    -i 256" "    -i 512"  \
          "-f  -i 1" "-f  -i 8" "-f  -i 64" "-f  -i 256" "-f  -i 512"  \
          "-ff -i 1" "-ff -i 8" "-ff -i 64" "-ff -i 256" "-ff -i 512" \
          "-b  -m 0" "-b  -m 64"   "-b  -m 256"   "-b  -m 1024"   "-b  -m 4096"    "-b  -m 16384" \
          "    -m 0" "    -m 64"   "    -m 256"   "    -m 1024"   "    -m 4096"    "    -m 16384" \
          "-f  -m 0" "-f  -m 64"   "-f  -m 256"   "-f  -m 1024"   "-f  -m 4096"    "-f  -m 16384" \
          "-ff -m 0" "-ff -m 64"   "-ff -m 256"   "-ff -m 1024"   "-ff -m 4096"    "-ff -m 16384" \
          "-k 8192" "-k 0" "-k 1", "-k 64" "-k 65656" \
          "-n 0 -x 0" "-n 1 -x 1" "-n 8 -x 16" "-n 500 -x 2000" \
          "-a 512" "-a 0" "-a 4096" "-a 65565" \
        )
    elif [[ "$1" == "pw" ]] ; then
      # Generate pseudo-pairwise options
      optlst=("")
      for ((i=0; i<${#optpw1[@]}; i++)); do    # 1 to 7
        for ((j=0; j<${#optpw2[@]}; j++)); do  # 1 to 5
          x="${optpw1[$i]} ${optpw2[$j]}"
          ((m=i     % ${#optpw3[@]})) && x="$x ${optpw3[$m]}"
          ((k=j     % ${#optpw4[@]})) && x="$x ${optpw4[$j]}"
          ((l=(i+j) % ${#optpw5[@]})) && x="$x ${optpw5[$l]}"
          x="${x//  / }"
          optlst+=("$x")
          echo ">$x"
        done
      done
      echo "Total number of options to test: ${#optlst[@]}"
      read -p "Continue in 15 seconds, or ctrl-c to abort... " -t 16 x
    else 
      optlst=("$@")
    fi
    optcnt=${#optlst[*]}

  #ruler   ---------1---------2---------3---------4---------5---------6---------7---------8
  #ruler   12345678901234567890123456789012345678901234567890123456789012345678901234567890
  echo -e "HDR JDF-file                  SIZE TIME            DATA /    OVH   /    EQL   OPTIONS"
  lng=0
  lst=$(ls $dir | sed -e 's/\..*$//' | uniq | uniq )
  for bse in $lst; do
    org=""
    for new in $dir/$bse.$ext; do
      if [[ ${new##*.} != "jdf" && ${new##*.} != "xdf" && ${new##*.} != "gz" && ${new##*.} != "tst" ]]
      then
        if [[ $org != "" ]]
        then
          if [[ -n "$xdelta" ]]
          then
            for opt in "" #"-6" "-9"
            do
              tme=unknown
              dif=${new%.*}.xdf
              echo "------------------------------------------------------------"
              echo "$xdelta $opt $org $new $dif"
              if [[ ! -f $dif ]]
              then
                (time nice -n 20 $xdelta $opt $org $new $dif) 2>$TEMP
                echo "Exit code = $?"

                cat $TEMP
                tme=$(grep "^real" $TEMP)
                tme=${tme##real}
              fi

              inp=$(ls --block-size=1 -s $org)
              inp=${inp%% $org*}

              out=$(ls --block-size=1 -s $dif)
              out=${out%% $dif*}

              echo -e XDF\\t$opt\\t$org\\t$new\\t$inp\\t$out\\t$tme
            done
          fi

          for ((optind=0; optind<optcnt; optind++))
          do
            opt=${optlst[optind]}
            dif=${new%.*}.$(printf %02d $optind).jdf

            # Switch between long/short options
            if [[ $lng == 1 ]]; then
              lng=0
              opt="${opt/-v /--verbose }"
              opt="${opt/-vv /--verbose -v }"
              opt="${opt/-f /--lazy }"
              opt="${opt/-ff /--lazy -f }"
              opt="${opt/-b /--better }"
              opt="${opt/-bb /--better -b }"
              opt="${opt/-k /--block-size }"
              opt="${opt/-x /--search-max }"
              opt="${opt/-s /--stdio }"
            fi
            if [[ $lng == 2 ]]; then
              lng=1
              opt="${opt//-vv /--verbose --verbose }"
              opt="${opt//-c /--console }"
              opt="${opt/-n /--search-min }"
            fi
            if [[ $lng == 3 ]]; then
              opt="${opt//-ff /--lazy --lazy }"
              opt="${opt//-bb /--better --better }"
              opt="${opt//-p /--sequential-source }"
              opt="${opt//-q /--sequential-dest }"
              lng=2
            fi
            if [[ $lng == 4 ]]; then
              # Put them all together
              opt="$(echo "$opt"|sed -e 's/-\([^0-9 ]*\) *-\([^0-9 ]*\)/-\1\2/g' \
                                     -e 's/-\([^0-9 ]*\) *-\([^0-9 ]*\)/-\1\2/g')"
              lng=3
            fi
            if [[ $lng == 0 ]]; then
              lng=4
            fi
            #echo ":$opt"
            #continue

            echo "------------------------------------------------------------"
            echo "$(date) $jdiff $opt $org $new $dif"
            #(time nice -n 20 $jdiff $opt $org $new $dif 2>$TEMP/jdiff.out) 2>$TEMP/jtst.out
            (time $jdiff $opt $org $new $dif 2>&1 | tee $TEMP.1) 2>$TEMP.2
            echo "Exit code = $?"

            #cat $TEMP/jdiff.out
            cat $TEMP.2
            tme=$(grep "^user" $TEMP.2)
            tme=${tme##user?}

            dtabyt=$(grep "^Data *bytes" $TEMP.1)
            dtabyt=${dtabyt##*=}
            eqlbyt=$(grep "^Equal *bytes" $TEMP.1)
            eqlbyt=${eqlbyt##*=}
            ovhbyt=$(grep "^Control-Esc *bytes" $TEMP.1)
            [[ -z "$ovhbyt" ]] && ovhbyt=$(grep "^Overhead *bytes" $TEMP.1)
            ovhbyt=${ovhbyt##*=}

            gzip -f $dif

            #inp=$(ls --block-size=1 -s $org)
            #inp=${inp%% $org*}
            inp=$(stat -c%s "$org")

            #out=$(ls --block-size=1 -s $dif.gz)
            #out=${out%% $dif*}
            out=$(stat -c%s "$dif.gz")

            echo "$(date) if zcat $dif.gz | $jpatch $org - | cmp -s $new"
            if [[ $typ = w32 ]]
            then
              gunzip $dif.gz
              time $jpatch $org $dif $new.tst
              if cmp -s $new $new.tst
              then sta=JDF
              else sta=ERR
              fi
              #then echo -e JDF\\t$dif\\t$out\\t$tme\\t$dtabyt/$eqlbyt/$ovhbyt\\t$opt
              #else echo -e ERR\\t$dif\\t$out\\t$tme\\t$dtabyt/$eqlbyt/$ovhbyt\\t$opt
              #fi
              gzip $dif
              rm $new.tst
            else
              if zcat $dif.gz | $jpatch $org - | cmp -s $new
              then sta=JDF
              else sta=ERR
              fi
              #then echo -e JDF\\t$dif\\t$out\\t$tme\\t$dtabyt/$eqlbyt/$ovhbyt\\t$opt
              #else echo -e ERR\\t$dif\\t$out\\t$tme\\t$dtabyt/$eqlbyt/$ovhbyt\\t$opt
              #fi
            fi
            #ruler  ---------1---------2---------3---------4---------5---------6---------7---------8
            #ruler  12345678901234567890123456789012345678901234567890123456789012345678901234567890
            printf "%s %-20.20s %9s %-11.11s %8s / %8s / %8s %s\n" \
              "$sta" "${dif#*/}" $out "$tme" $dtabyt $ovhbyt $eqlbyt "$opt"
          done
        fi
        org=$new
      fi
    done
  done 2>&1
else
  org=$1; shift
  new=$1; shift
  echo "$jdiff $* $org $new | $jpatch $org - | cmp $new"
  if $jdiff $* $org $new| $jpatch $org - | cmp $new
  then echo JDF $* $org $new OK
  else echo ERR $* $org $new ERR
  fi
fi
