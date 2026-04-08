package com.example.demo.dto;

import lombok.Data;

import java.util.List;

@Data
public class PageVO<T> {
    private List<T> records;
    private long total;
    private int size;
    private int current;
    private int pages;
}

