export default background;
export namespace background {
    let available: boolean;
    function register(): Promise<never>;
    function schedule(): Promise<never>;
    function cancel(): Promise<never>;
    function status(): Promise<never>;
}
