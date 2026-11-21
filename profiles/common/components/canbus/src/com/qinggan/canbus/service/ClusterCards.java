package com.qinggan.canbus.service;

import android.os.Parcel;

import java.util.Arrays;
import java.util.Comparator;

/**
 * The card map of syncMenuCardInfo, as Parcel.writeMap writes it from the Settings display page:
 * per entry a CardInfo Parcelable (creator name, ordinal, value) and an Integer menu position.
 */
final class ClusterCards {
    private static final int VAL_INTEGER = 1, VAL_PARCELABLE = 4;

    private ClusterCards() {}

    /** The CardInfo values ordered by their menu position, as the OEM sorts them. */
    static int[] read(Parcel data) {
        int count = data.readInt();

        if (count < 0) {
            return new int[0];
        }

        // The 0x75 frame counts the cards in one byte.
        if (count > 255) {
            throw new IllegalArgumentException("too many cluster cards");
        }

        int[][] cards = new int[count][];

        for (int i = 0; i < count; i++) {
            if (data.readInt() != VAL_PARCELABLE || data.readString() == null) {
                throw new IllegalArgumentException("cluster card required");
            }

            data.readInt();
            int value = data.readInt();

            if (data.readInt() != VAL_INTEGER) {
                throw new IllegalArgumentException("cluster card position required");
            }

            cards[i] = new int[] {data.readInt(), value};
        }

        Arrays.sort(cards, Comparator.comparingInt(card -> card[0]));
        int[] values = new int[count];

        for (int i = 0; i < count; i++) {
            values[i] = cards[i][1];
        }

        return values;
    }
}
