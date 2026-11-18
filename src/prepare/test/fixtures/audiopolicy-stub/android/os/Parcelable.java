package android.os;

public interface Parcelable {
    int describeContents();

    void writeToParcel(Parcel parcel, int flags);
}
