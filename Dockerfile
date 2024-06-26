FROM swift
WORKDIR /test
COPY signal_for_suspend.cpp ./
RUN clang signal_for_suspend.cpp -o signal_for_suspend
CMD ["./signal_for_suspend"]
